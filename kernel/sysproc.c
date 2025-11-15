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

int sys_sigreturn(void)
{
  return 0; 
}