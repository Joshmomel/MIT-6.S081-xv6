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

#define PGCNT (PHYSTOP / PGSIZE + 10)
struct spinlock cntlock;
int ref_cnt[PGCNT];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&cntlock, "ref_cnt");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    acquire(&cntlock);
    ref_cnt[PGINDEX((uint64)p)] = 1;
    release(&cntlock);
    kfree(p);
  }
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
    panic("kfree: invalid pa");

  acquire(&cntlock);
  ref_cnt[PGINDEX((uint64)pa)]--;
  if (ref_cnt[PGINDEX((uint64)pa)] > 0)
  {
    release(&cntlock);
    return;
  }
  release(&cntlock);
  panic_on(ref_cnt[PGINDEX((uint64)pa)] != 0, "kfree: mapped");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&cntlock);
    panic_on(ref_cnt[PGINDEX((uint64)r)] != 0, "rcnt");
    ref_cnt[PGINDEX((uint64)r)] = 1;
    release(&cntlock);
  }
  return (void*)r;
}

// Modify reference count
void
kmref(uint64 pa, int delta)
{
  panic_on(PGINDEX(pa) > PGCNT, "invalid pa");
  acquire(&cntlock);
  ref_cnt[PGINDEX(pa)] += delta;
  panic_on(ref_cnt[PGINDEX(pa)] < 0, "strange ref");
  release(&cntlock);
}