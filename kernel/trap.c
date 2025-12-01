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


int cowfault(pagetable_t pagetable, uint64 fault_va){
    // uint64 fault_va = r_stval();
    pte_t *pte = walk(pagetable, fault_va, 0);
    if (pte == 0|| (*pte & PTE_V) == 0)
      panic("COW fault: pte is NULL");

    if ( (*pte & PTE_COW) && ((*pte & PTE_W) == 0)){ 
      uint64 pa = PTE2PA(*pte);

      // page ref count ==1,
      // because it is the only one using this page,we just clear the COW flag and set WRITE flag
      if(page_ref_get(pa)==1){
        *pte = PA2PTE(pa) | ((PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW);
        sfence_vma();
        return 0;
      }
      else{
        // if page ref count >1, we need to make a copy of the page
        uint64 *pa2 = kalloc();
        if(pa2==0){
          printf("cowfault error: pa2=0, usertrap scause=0x%p sepc=0x%p stval=0x%p\n",
          r_scause(), r_sepc(), r_stval());
          return -1;
        }
        // copy contents from old page to new page
        // printf("COW page-fault handler: copy contents from old page to new page, mem=%p,pa=%p\n",mem, pa);
        memmove((void *)pa2, (void *)pa, PGSIZE);
        // decrement old page refcount (page_ref_dec(oldpa)) only once
        page_ref_dec(pa);
        // install new physical page in pte with write permission
        *pte = PA2PTE(pa2) | ((PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW);
        sfence_vma();
        return 0;
    }
  }
    printf("cowfault not a cow page: usertrap scause=0x%p sepc=0x%p stval=0x%p\n",
        r_scause(), r_sepc(), r_stval());
    return -1;
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
  } else if((which_dev = devintr()) != 0){
    // ok
 }else if(r_scause()==15){
    // handle store page fault for COW fork
    // store/AMO page fault = 15(0x000000000000000f)   
    if((cowfault(p->pagetable, r_stval()) )<0){
      p->killed =1;
  }
}
  else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());


    // diagnostic: print PTE for faulting VA (insert where you print unexpected scause)
    uint64 va = r_stval();   // faulting virtual address
    pte_t *pte = walk(myproc()->pagetable, PGROUNDDOWN(va), 0);
    printf("diagnose: fault va 0x%p\n", va);
    if(pte == 0){
      printf("  walk returned NULL pte\n");
    } else {
      uint64 ptev = *pte;
      printf("  pte = 0x%p\n", ptev);
      printf("  PTE_V:%d PTE_R:%d PTE_W:%d PTE_X:%d PTE_U:%d PTE_COW(software):0x%p\n",
        (ptev & PTE_V) != 0,
        (ptev & PTE_R) != 0,
        (ptev & PTE_W) != 0,
        (ptev & PTE_X) != 0,
        (ptev & PTE_U) != 0,
        (ptev & PTE_COW) != 0
      );
      printf("  PPN/PA = 0x%p\n", PTE2PA(ptev));
  }
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

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