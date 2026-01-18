// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define STEAL_PAGES 8

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
} ;


struct kmem kmems[NCPU];

void
kinit()
{
  // initialize the allocator for one single freelist
  // initlock(&kmem.lock, "kmem");
  // freerange(end, (void*)PHYSTOP);

  // when use multiple freelist, you need to initialize multiple freelist for each cpu
  for(int i=0; i<NCPU; i++) {
    initlock(&kmems[i].lock, "kmem");
    kmems[i].freelist = 0;
  }
  // put physical memory into freelist of CPU 0 when initializing the allocator
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);

  int id= cpuid();
  acquire(&kmems[id].lock);
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // acquire(&kmem.lock);
  // r = kmem.freelist;

  // if(r)
    // kmem.freelist = r->next;
  // release(&kmem.lock);

  // 1. try local cpu freelist
  int id= cpuid();
  acquire(&kmems[id].lock);
  r = kmems[id].freelist;
  if(r){
    kmems[id].freelist = r->next;
    release(&kmems[id].lock);
    memset((char*)r, 5, PGSIZE); // fill with junk
    return r;
  }
  release(&kmems[id].lock);

  
  // 2.  steal from other cpus (NO local lock held)
  for(int i=0; i<NCPU; i++){
    if(i==id) continue;
    // acquire the lock of the other cpu
    acquire(&kmems[i].lock);
    if(kmems[i].freelist){
      r = kmems[i].freelist;
      kmems[i].freelist = r->next;
      release(&kmems[i].lock); // release the lock of the other cpu
      memset((char *)r, 5, PGSIZE);
      return r;
    }
    release(&kmems[i].lock);
  }

  return 0;
}
