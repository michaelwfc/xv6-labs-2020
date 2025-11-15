#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  }else if((which_dev = devintr()) != 0){
    // ==============lab4-traps-part3==============
    /**
    devintr() checks hardware interrupt pending bits and returns:
    2 → a timer interrupt (from the local timer)
    1 → some external device interrupt (e.g., UART, disk)
    0 → not a device interrupt

    when timer trap, That is: the clock ticked. we need to update the process's ticks

    p->trapframe ->epc = (uint64)p->alarm_handler;
    Force the process to return to user-space at the handler.
    By changing the trapframe's epc, the next sret will resume at handler.
    p->trapframe->epc is where control will resume in user mode (the program counter saved on trap). 
    Overwriting it makes the kernel return into the user handler.
    For test0 we don’t restore the original epc after handler returns (that’s for later parts). So after the handler runs, 
    the process may crash — but the handler does print alarm! first.
    */
    if(which_dev==2){
      if(p->alarm_interval >0){
        p->alarm_ticks --;
        if(p->alarm_ticks<=0 && !p->alarm_active){
          // avoid re-entrant
          p->alarm_active = 1;

          // save current trampframe into alarm trampframe,so that sig_return can restore later
          // memmove(p->alarm_trapframe,p->trapframe, sizeof(struct trapframe));
          // save all the needed registers
          p->saved_epc = p->trapframe->epc; // saved user program counter
          p->saved_ra = p->trapframe->ra;
          p->saved_sp = p->trapframe->sp;
          p->saved_gp = p->trapframe->gp;
          p->saved_tp = p->trapframe->tp;
          p->saved_t0 = p->trapframe->t0;
          p->saved_t1 = p->trapframe->t1;
          p->saved_t2 = p->trapframe->t2;
          p->saved_t3 = p->trapframe->t3;
          p->saved_t4 = p->trapframe->t4;
          p->saved_t5 = p->trapframe->t5;
          p->saved_t6 = p->trapframe->t6;
          p->saved_a0 = p->trapframe->a0;
          p->saved_a1 = p->trapframe->a1;
          p->saved_a2 = p->trapframe->a2;
          p->saved_a3 = p->trapframe->a3;
          p->saved_a4 = p->trapframe->a4;
          p->saved_a5 = p->trapframe->a5;
          p->saved_a6 = p->trapframe->a6;
          p->saved_a7 = p->trapframe->a7;
          p->saved_s0 = p->trapframe->s0;
          p->saved_s1 = p->trapframe->s1;
          p->saved_s2 = p->trapframe->s2;
          p->saved_s3 = p->trapframe->s3;
          p->saved_s4 = p->trapframe->s4;
          p->saved_s5 = p->trapframe->s5;
          p->saved_s6 = p->trapframe->s6;
          p->saved_s7 = p->trapframe->s7;
          p->saved_s8 = p->trapframe->s8;
          p->saved_s9 = p->trapframe->s9;
          p->saved_s10 = p->trapframe->s10;
          p->saved_s11 = p->trapframe->s11;


          p->trapframe ->epc = (uint64)p->alarm_handler;
          // reset ticks
          p->alarm_ticks = p->alarm_interval;

          
        }
      }
    yield();}
    // give up the CPU if this is a timer interrupt.
    /**
    - It marks the current process as runnable again:  p->state = RUNNABLE;
    - Then it switches context to the scheduler via sched(), allowing another process to run.
    - Later, the scheduler may choose to resume this same process or another one.

    In effect:
    On each timer interrupt, the currently running user process voluntarily gives up the CPU so the scheduler can run another process. 
    This is how preemptive multitasking is implemented in xv6.

    Why we need it
    Without this yield() on timer interrupts, a user process running in a tight loop (say, while(1);) would never release the CPU. 
    No other process could run until it made a blocking system call. The system would effectively be single-tasking.


    With the timer interrupt → yield() path: “You’ve had your turn, user process. Let someone else run for a bit.”
    - Every few ticks, the clock interrupt forces a context switch.
    - The kernel regains control, runs the scheduler, and shares CPU time fairly.


    Process p:          CPU scheduler:
    -------------       --------------------
    yield()
      -> sched()
          swtch(&p->context, &cpu->context)
                          -----> resumes scheduler()
                                  picks next RUNNABLE q
                                  swtch(&cpu->context, &q->context)
                                                  -----> resumes process q
    */
        
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);



  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

