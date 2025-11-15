#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  backtrace();
  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


void sys_demo1(void)
{
  // int n;
  // if(argint(0, &n) < 0)
  //   return -1;
  // return sum_to(n);
  demo1();
}

void sys_demo2(void)
{
  demo2();
}

void sys_demo3(void)
{
  demo3();
}

void sys_demo4(void)
{
  demo4();
}

void sys_demo6(void){
  demo6();
}


// ==============lab4-traps-part3==============
uint64 sys_sigalarm()
{
  int ticks;
  uint64 handler;
  // argint and argaddr are the usual xv6 helpers. 
  // argaddr reads an argument interpreted as an address (user pointer / integer).
  if(argint(0, &ticks)<0)
    return -1;
  if(argaddr(1, &handler)<0)
    return -1;

  struct proc *p = myproc();
  if(ticks <=0){
    p->alarm_interval = 0;
    p->alarm_ticks = 0;
    p->alarm_handler=0;
    return 0;
  }
  p->alarm_interval = ticks;
  p->alarm_ticks = ticks;
  // p->alarm_handler = handler;
  p->alarm_handler = (void (*)())handler;
  return 0;
}

/**
sys_sigreturn 会做三件事：
1. 把 trapframe 恢复成被中断时的版本（你在 usertrap 保存的）
2. 允许 alarm 再次触发（取消正在 handler 内的“锁”）
3. 返回用户态

sys_sigreturn()
↓
return from syscall()
↓
usertrapret()
↓
sret
↓
pc = 原 trapframe.epc   （就是中断前的 foo() 那个点）


*/
int sys_sigreturn(void)
{
  struct proc *p = myproc();
  // retore the trapframe from alarm_trapframe
  // memmove(p->trapframe, p->alarm_trapframe, sizeof(struct trapframe));
  // restore all the saved registers
  p->trapframe->epc = p->saved_epc; 
  p->trapframe->ra = p->saved_ra; 
  p->trapframe->sp = p->saved_sp; 
  p->trapframe->gp = p->saved_gp; 
  p->trapframe->tp = p->saved_tp; 
  p->trapframe->a0 = p->saved_a0; 
  p->trapframe->a1 = p->saved_a1; 
  p->trapframe->a2 = p->saved_a2; 
  p->trapframe->a3 = p->saved_a3; 
  p->trapframe->a4 = p->saved_a4; 
  p->trapframe->a5 = p->saved_a5; 
  p->trapframe->a6 = p->saved_a6; 
  p->trapframe->a7 = p->saved_a7; 
  p->trapframe->t0 = p->saved_t0; 
  p->trapframe->t1 = p->saved_t1; 
  p->trapframe->t2 = p->saved_t2; 
  p->trapframe->t3 = p->saved_t3; 
  p->trapframe->t4 = p->saved_t4; 
  p->trapframe->t5 = p->saved_t5; 
  p->trapframe->t6 = p->saved_t6;
  p->trapframe->s0 = p->saved_s0;
  p->trapframe->s1 = p->saved_s1;
  p->trapframe->s2 = p->saved_s2;
  p->trapframe->s3 = p->saved_s3;
  p->trapframe->s4 = p->saved_s4;
  p->trapframe->s5 = p->saved_s5;
  p->trapframe->s6 = p->saved_s6;
  p->trapframe->s7 = p->saved_s7;
  p->trapframe->s8 = p->saved_s8;
  p->trapframe->s9 = p->saved_s9;
  p->trapframe->s10 = p->saved_s10;
  p->trapframe->s11 = p->saved_s11;


  p->alarm_active = 0;
  return 0;
}