// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define NPAGES (PHYSTOP/PGSIZE)

// trace the last ref_count of each page
const char * page_ref_trace[NPAGES];

static struct spinlock page_ref_lock;
static int page_ref[NPAGES];  // zero-initialized at boot

// helper: physical addr -> index
static inline int
pa_to_idx(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}


// helper: physical addr -> kernel virtual addr
// static inline char*
// pa2kva(uint64 pa)
// {
//   return (char*)(pa + KERNBASE);
// }

// void page_ref_init(void){
//   // initlock(&page_ref_lock, "page_ref")
//   acquire(&page_ref_lock);
//   memset(page_ref, 0, sizeof(page_ref));
//   release(&page_ref_lock);
// }


int page_ref_get(uint64 pa){ 
  int v;
  int index = pa_to_idx(pa);
  acquire(&page_ref_lock);
  if(index < 0 || index >= NPAGES){
    printf("page_ref_get: pa=0x%p index=%d -> %d\n", pa, index, page_ref[index]);
    panic("page_ref_get: bad pa");
  }
  v = page_ref[index];
  release(&page_ref_lock);
  return v;
}

void _page_ref_inc(uint64 pa){
  int index = pa_to_idx(pa);
 
  if(index < 0 || index >= NPAGES){
    printf("page_ref_inc: pa=0x%p index=%d -> %d\n", pa, index, page_ref[index]);
    panic("page_ref_inc: bad pa");
  }
  acquire(&page_ref_lock);
  page_ref[index]++;
  release(&page_ref_lock);
}

void page_ref_inc_debug(uint64 pa, const char *why) {
  int idx = pa_to_idx(pa);
  if(idx < 0 || idx >= NPAGES){
    panic("page_ref_inc: bad pa");
  }

  acquire(&page_ref_lock);
  page_ref[idx] += 1;
  page_ref_trace[idx] = why;   // 记录来源
  // printf("INC pa=0x%p idx=%d -> ref=%d (%s)\n",pa, idx, page_ref[idx], why);
  release(&page_ref_lock);
}




void
kfree_inner(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void _page_ref_dec(uint64 pa){
  int index = pa_to_idx(pa);
  
  if(index < 0 || index >= NPAGES){
    panic("page_ref_dec: bad pa");
  }

  acquire(&page_ref_lock);

  if(page_ref[index]<=0){
    printf("page_ref_dec : pa=%p, index=%d, ref_count=%d\n", pa, index, page_ref[index]);
    panic("page_ref_dec: refcount <= 0");
  }

  if (page_ref[index]==0){
     // only when refcount drops to 0 do we actually free the physical page
    // char * kva= pa2kva(pa);
    kfree_inner((void *)pa);
    release(&page_ref_lock);
    return;
  }else{
  page_ref[index]--;
  release(&page_ref_lock);
  return;

}
 
}


void page_ref_dec_debug(uint64 pa, const char *why) {
  int idx = pa_to_idx(pa);

  if(idx < 0 || idx >= NPAGES){
    panic("page_ref_dec: bad pa");
  }

  acquire(&page_ref_lock);

  if(page_ref[idx] < 0){
    printf("[BUG] DEC pa=0x%p idx=%d ref=%d (%s)\n",
           pa, idx, page_ref[idx], why);
    printf("Last change: %s\n", page_ref_trace[idx]);
    panic("page_ref_dec: refcount <= 0");
  }
  
  //  when freerange in kinit(), page_ref[idx] first initalied ==0, and then kfree ->page_ref_dec(), 
  // we need free these page
  if(page_ref[idx] ==0){
    kfree_inner((void *)pa);
    release(&page_ref_lock);
    return;
  }

  
  page_ref[idx] -= 1;
  page_ref_trace[idx] = why;
  // printf("DEC pa=0x%p idx=%d -> ref=%d (%s)\n", pa, idx, page_ref[idx], why);
  
  if (page_ref[idx]==0){
    // only when refcount drops to 0 do we actually free the physical page
    // char * kva= pa2kva(pa);
    kfree_inner((void *)pa);
    release(&page_ref_lock);
    return;
  }
  release(&page_ref_lock);
  return;
}



void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_ref_lock, "page_ref_lock");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  { 
    // initial ref count is 1
    // page_ref_inc_debug((uint64)p, "freerange");
    page_ref[(uint64) p/PGSIZE] =0;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void * pa){
  // instead of immediately freeing, decrement refcount
  // printf("KFREE pa=0x%p idx=%d\n", pa, pa_to_idx((uint64)pa));
  page_ref_dec((uint64)pa);
}




// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    {// set refcount to 1
    // if r is kernel virtual addr, convert: pa = (uint64)r - KERNBASE;
    // printf("kalloc: pa=%p, index=%d\n", r,pa_to_idx((uint64) r));
    page_ref_inc((uint64)r);  
    memset((char*)r, 5, PGSIZE); // fill with junk
  }
  return (void*)r;
}


