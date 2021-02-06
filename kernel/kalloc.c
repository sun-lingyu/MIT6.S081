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
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

void
kinit()
{
  //only cpu0 can invoke this funciton.(refer to main.c to find details)
  //initialize locks
  for(int i=0;i<NCPU;i++)
    initlock(&kmem.lock[i], "kmem");
  freerange(end, (void*)PHYSTOP);//Let freerange give all free memory to the CPU running freerange.
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

  push_off();
  acquire(&kmem.lock[cpuid()]);

  r->next = kmem.freelist[cpuid()];
  kmem.freelist[cpuid()] = r;
  
  release(&kmem.lock[cpuid()]);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  acquire(&kmem.lock[cpuid()]);
  r = kmem.freelist[cpuid()];
  if(r)
  {
    kmem.freelist[cpuid()] = r->next;
    release(&kmem.lock[cpuid()]);
  }
  else
  {
    //steal from other cpus.
    struct run *r1;
    //how to avoid deadlock is a big issue!!!(even if deadlocks are not avoided, we may also pass the test!)
    //first give up my own lock, and then acquire lock in sequence!
    release(&kmem.lock[cpuid()]);
    for(int j=0;j<NCPU;j++)
    {
      if(j==cpuid())
        continue;
      acquire(&kmem.lock[j]);
      r1=kmem.freelist[j];
      if(r1)
      {
        r=r1;
        kmem.freelist[j]=r->next;
        release(&kmem.lock[j]);
        break;
      }
      release(&kmem.lock[j]);
    }
  }
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
