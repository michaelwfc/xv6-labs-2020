// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13   // prime to reduce collisions

static inline uint 
bhash(uint dev, uint blockno)
{
  // return (dev ^ blockno) % NBUCKET;
  return (dev*17+blockno) % NBUCKET;
}

struct bcache_bucket{
  struct spinlock lock;
  // head is already a pointer, It points to the first struct buf in the linked list
  struct buf *head; // singly-linked list is enough
};



// Original global bcache
// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;


// New global bcache
struct {
  struct spinlock eviction_lock; // serialize eviction only
  struct buf buf[NBUF];
  struct bcache_bucket buckets[NBUCKET];
} bcache;


/*
Original xv6 design
xv6’s buffer cache is built around one global structure:
- a doubly-linked list
- ordered by “most recently used” (MRU near head)
- protected by one global lock

This gives you:
- Simple eviction: reuse head.prev (LRU)
- Simple correctness
- Terrible scalability

The LRU list is the source of contention.


New design philosophy
Your lab goal is not “perfect LRU”.
Your goal is: Reduce lock contention to (almost) zero.
So we deliberately throw away the global ordering structure.
That’s why there is:
- ❌ no bcache.head
- ❌ no global list
- ❌ no global LRU lock

Instead:
- Buffers live in hash buckets
- Each bucket has its own lock
- There is no total order across all buffers

*/

// void
// binit(void)
// {
//   struct buf *b;

//   initlock(&bcache.lock, "bcache");

//   // Create linked list of buffers
//   bcache.head.prev = &bcache.head;
//   bcache.head.next = &bcache.head;
//   for(b = bcache.buf; b < bcache.buf+NBUF; b++){
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     initsleeplock(&b->lock, "buffer");
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
// }


// New binit 
void binit(void){
  struct buf *b;
  initlock(&bcache.eviction_lock, "bcache.eviction");

  for(int i=0;i<NBUCKET; i++){
    // initial spinlock for each bucket
    initlock(&bcache.buckets[i].lock, "bcache.bucket");
    bcache.buckets[i].head = 0;
  }

  for(b = bcache.buf; b<bcache.buf +NBUF; b++){
    // why no bcache.head, no global LRU list? 
    // No ordering structure. LRU will be handled implicitly via timestamps or simple eviction scan.
    b->refcnt = 0;
    b->valid = 0;
    b->next = 0;
    initsleeplock(&b->lock, "buffer");
  }

}



// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// static struct buf*
// bget(uint dev, uint blockno)
// {
//   struct buf *b;

//   acquire(&bcache.lock);

//   // Is the block already cached?
//   for(b = bcache.head.next; b != &bcache.head; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       b->refcnt++;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   // Not cached.
//   // Recycle the least recently used (LRU) unused buffer.
//   for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
//     if(b->refcnt == 0) {
//       b->dev = dev;
//       b->blockno = blockno;
//       b->valid = 0;
//       b->refcnt = 1;
//       release(&bcache.lock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   panic("bget: no buffers");
// }






/* 


New bget
1. Compute bucket
2. Look up (dev, blockno) under bucket lock
3. If found → increment refcnt → lock buffer → return
4. If not found → eviction path
   Eviction is serialized 
   Victim buffer is removed from old bucket (if any)
   Insert victim into new bucket
5. Return locked buffer


Then how do we evict without LRU?
This sentence is key: “LRU will be handled implicitly via timestamps or simple eviction scan.”
That means one of two acceptable strategies:
1. Option A (simplest, and what I showed)
- Scan all buffers
- Pick any buffer with refcnt == 0
- No ordering, no timestamps
- Eviction is rare → cost acceptable
This is not LRU, but it’s correct.

2. Option B (optional improvement)
- Add uint last_used
- Update it in brelse
- Evict smallest timestamp
The lab does not require true LRU.

Correctness > optimal eviction.


*/
static struct buf*
bget(uint dev, uint blockno)
{
  uint h = bhash(dev, blockno);
  struct buf* b;

  // 1. Fast path: look up
  // lock is a struct → functions expect a pointer to it → you use &
  acquire(&bcache.buckets[h].lock);
  // Lookup: O(NBUF) -> O(bucket size)
  // head is already a pointer → you use it directly → no &
  for(b = bcache.buckets[h].head; b; b=b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[h].lock);
      acquiresleep(&b->lock);  // why? 
      return b;
    }
  }
  release(&bcache.buckets[h].lock);


  // 2. Miss: eviction (serialized)
  // “Serialized eviction” means: Only one CPU at a time is allowed to choose and recycle a free buffer.
  // Locking: One bcache.lock -> Per-bucket locks + eviction lock
  // Imagine two CPUs both miss the cache:
  // CPU 0 picks buffer A
  // CPU 1 picks buffer A
  // Both reassign it to different blocks,  Boom: invariant violated.
  // So eviction must be protected by one lock.
  
  acquire(&bcache.eviction_lock);
  // This bcache.eviction_lock: 
  // - Is rarely contended
  // - Covers a slow path
  // - Is acceptable

  
  // Retry lookup after acquiring eviction lock
  acquire(&bcache.buckets[h].lock);
  for(b= bcache.buckets[h].head; b; b=b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[h].lock);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);  // why? 
      return b;
    }
  } 
  release(&bcache.buckets[h].lock);

  // 3. Find victim
  struct buf *victim = 0;
  for(b= bcache.buf;b< bcache.buf+ NBUF; b++){
    if(b->refcnt == 0){
      victim = b;
      break;
    }
  }
  if(!victim){
    panic("[bget] No free buffer");
  }

  // 4. remove from old bucket(if need)
  /* Why a buffer might be in an “old bucket” ?
  When we recycle a buffer:
  - Old (dev, blockno) → old bucket
  - New (dev, blockno) → new bucket
  These two buckets may be different.
  So we must:
  1. Remove the buffer from the old bucket
  2. Insert it into the new bucket

  // pp walks the linked list
  // Removes victim safely
  */
  if(victim->refcnt==0 && victim->valid){
    // Because valid == 1, this buffer is currently linked into some bucket list corresponding to (old_dev, old_block).
    // We are about to reuse this same struct buf for a different block.
    uint oldh = bhash(victim->dev, victim->blockno);

    // Why acquire bcache.buckets[oldh].lock?
    // Because: 
    // Other CPUs may be traversing that bucket
    // Or inserting into that bucket
    // Or releasing buffers in that bucket
    acquire(&bcache.buckets[oldh].lock);

    /* Think of the bucket list as: head -> b1 -> b2 -> b3 -> NULL
     pp always points to the pointer that points to the current node.
    That means:
    - Initially: pp points to head
    - Later: pp points to b1->next, then b2->next, etc
    Using struct buf **pp allows us to delete a node without special-casing head.
    */
    struct buf **pp = &bcache.buckets[oldh].head;
    // *pp is the current node
    while(*pp){
      if(*pp == victim){
        // If it’s the victim:
        // Replace the pointer that points to it
        // Skip over the victim
        // This single line does the unlink:
        *pp = victim->next;
        break;
      }
      // Otherwise: Advance pp to point to the next pointer
      pp = &(*pp)->next;
    }
    release(&bcache.buckets[oldh].lock);
  }

  // 5. initialize victim
  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;

  // 6. insert into new bucket
  acquire(&bcache.buckets[h].lock);
  victim->next = bcache.buckets[h].head;
  bcache.buckets[h].head = victim;
  release(&bcache.buckets[h].lock);
  release(&bcache.eviction_lock);
  acquiresleep(&victim->lock);
  return victim;
}


// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");

  // printf("[bwrite] block %d\n", b->blockno);
  
  virtio_disk_rw(b, 1);
}

/*
Original xv6 responsibilities of brelse
brelse had to:
- Drop refcnt
- Move buffer to MRU position
- Maintain LRU list invariants

That forced:
- Global lock
- Pointer manipulation
- Contention

*/
// // Release a locked buffer.
// // Move to the head of the most-recently-used list.
// void
// brelse(struct buf *b)
// {
//   if(!holdingsleep(&b->lock))
//     panic("brelse");

//   releasesleep(&b->lock);

//   acquire(&bcache.lock);
//   b->refcnt--;
//   if (b->refcnt == 0) {
//     // no one is waiting for it.
//     b->next->prev = b->prev;
//     b->prev->next = b->next;
//     b->next = bcache.head.next;
//     b->prev = &bcache.head;
//     bcache.head.next->prev = b;
//     bcache.head.next = b;
//   }
  
//   release(&bcache.lock);
// }

/*
New design
brelse only needs to do one thing: “This buffer is no longer in use.”
b->refcnt--;
And it must be:
- Atomic
- Bucket-local

So:
acquire(&bcache.bucket[h].lock);
b->refcnt--;
release(&bcache.bucket[h].lock);

No global structure.
No ordering.
No contention storm.

This is the big win of the redesign.

*/
void brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  uint h =bhash(b->dev, b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  release(&bcache.buckets[h].lock);
}

// void
// bpin(struct buf *b) {
//   acquire(&bcache.lock);
//   b->refcnt++;
//   release(&bcache.lock);
// }

/*
refcnt is now protected by bucket locks
bpin / bunpin only touch refcnt
refcnt is protected by bucket lock
No eviction can occur while refcnt > 0
Logging layer remains correct

*/
void bpin(struct buf *b){
  uint h = bhash(b->dev, b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt++;
  release(&bcache.buckets[h].lock);
}

// void
// bunpin(struct buf *b) {
//   acquire(&bcache.lock);
//   b->refcnt--;
//   release(&bcache.lock);
// }

void bunpin(struct buf *b){
  uint h = bhash(b->dev, b->blockno);
  acquire(&bcache.buckets[h].lock);
  b->refcnt--;
  release(&bcache.buckets[h].lock);
}

