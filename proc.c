#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#ifdef CS333_P2
#include "uproc.h"
#endif  //CS333_P2

static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

#ifdef CS333_P3
// record with head and tail pointer for constant-time access to the beginning
// and end of a linked list of struct procs.  use with stateListAdd() and
// stateListRemove().
struct ptrs {
  struct proc* head;
  struct proc* tail;
};
#endif

#ifdef CS333_P3
#define statecount NELEM(states)
#endif  //CS333_P3

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
#ifdef CS333_P3
  struct ptrs list[statecount];
#endif  //CS333_P3
} ptable;

// list management function prototypes
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc*, enum procstate, const char *, int);
#endif

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);  //get lock

#ifdef CS333_P3
  if(!ptable.list[UNUSED].head)
  {
    release(&ptable.lock);
    return 0;
  }
  else
    p = ptable.list[UNUSED].head;
#else
  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
#endif
#ifdef CS333_P3
  if(stateListRemove(&ptable.list[UNUSED], p) == -1)
  {
    panic("stateListRemove in allocproc (UNUSED -> EMBRYO)");
  }
  assertState(p, UNUSED, __FUNCTION__, __LINE__);
#endif  //CS333_P3

  p->state = EMBRYO;
  p->pid = nextpid++;

#ifdef CS333_P3
  stateListAdd(&ptable.list[EMBRYO], p); 
#endif  //CS333_P3

  release(&ptable.lock);  //release lock

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    acquire(&ptable.lock);  //get lock
#ifdef CS333_P3
    if(stateListRemove(&ptable.list[EMBRYO], p) == -1)
    {
      panic("stateListRemove in allocproc (EMBRYO -> UNUSED)");
    }
    assertState(p, EMBRYO, __FUNCTION__, __LINE__);
#endif  //CS333_P3

    p->state = UNUSED;

#ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], p);
#endif  //CS333_P3
    release(&ptable.lock);  //release lock
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
#ifdef CS333_P1
  p->start_ticks = ticks;
#endif //CS333_P1
#ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in =0;
#endif  //CS333_P2
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
#ifdef CS333_P3
  acquire(&ptable.lock);
  initProcessLists();
  initFreeList();
  release(&ptable.lock);
#endif  //CS333_P3

  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

#ifdef CS333_P2
  p->uid = UID;
  p->gid = GID;
#endif  //CS333_P2

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);  //getlock
#ifdef CS333_P3
  if(stateListRemove(&ptable.list[EMBRYO], p) == -1)
  {
    panic("stateListRemove() in userinit() error");
  }
  assertState(p, EMBRYO, __FUNCTION__, __LINE__);
#endif  //CS333_P3
  
  p->state = RUNNABLE;

#ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], p);
#endif  //CS333_P3
  release(&ptable.lock);  //release lock
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;

    acquire(&ptable.lock);  //get lock
#ifdef CS333_P3
    if(stateListRemove(&ptable.list[EMBRYO], np) == -1)
    {
      panic("stateListRemove in fork, EMBRYO -> UNUSED");
    }
    assertState(np, EMBRYO, __FUNCTION__, __LINE__);
#endif  //CS333_P3

    np->state = UNUSED;

#ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], np);
#endif  //CS333_P3
    release(&ptable.lock);  //release lock
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

#ifdef CS333_P2
  np->uid = curproc->uid;
  np->gid = curproc->gid;
#endif  //CS333_P2

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);  //get lock
#ifdef CS333_P3
  if(stateListRemove(&ptable.list[EMBRYO], np) == -1)
  {
    panic("stateListRemove() in fork() error");
  }
  assertState(np, EMBRYO, __FUNCTION__, __LINE__);
#endif  //CS333_P3

  np->state = RUNNABLE;

#ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], np);
#endif  //CS333_P3
  release(&ptable.lock);  //release lock
  return pid;
}

#ifdef CS333_P3
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);  //get lock

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init. 
  for(int i = EMBRYO; i <= ZOMBIE; ++i)
  {
    p = ptable.list[i].head;
    while(p)
    {
      if(p->parent == curproc)
      {
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
        p = p->next;
      }
      else
        p = p->next;
    }
  }

  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1)
  {
    panic("stateListRemove() in exit() error");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  stateListAdd(&ptable.list[ZOMBIE], curproc);

#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}

#else // if not CS333_P3
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
#ifdef PDX_XV6
  curproc->sz = 0;
#endif // PDX_XV6
  sched();
  panic("zombie exit");
}
#endif  //CS333_P3


#ifdef CS333_P3
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //get lock
  for(;;){
    // Scan through lists looking for exited children.
    havekids = 0;
    for(int i = EMBRYO; i <= ZOMBIE; ++i)
    {
      p = ptable.list[i].head;
      while(p)
      {
        if(p->parent == curproc)
        {
          havekids = 1;
          if(p->state == ZOMBIE){
            // Found one.
            pid = p->pid;
            kfree(p->kstack);
            p->kstack = 0;
            freevm(p->pgdir);
            p->pid = 0;
            p->parent = 0;
            p->name[0] = 0;
            p->killed = 0;
            //remove from ZOMBIE list and add to UNUSED
            if(stateListRemove(&ptable.list[ZOMBIE], p) == -1)
            {
              panic("stateListRemove() in wait() error");
            }
            assertState(p, ZOMBIE, __FUNCTION__, __LINE__);

            p->state = UNUSED;

            stateListAdd(&ptable.list[UNUSED], p);

            release(&ptable.lock);
            return pid;
          }
          p = p->next;
        }
        else
          p = p->next;
      }
    }
    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

#else //if not CS333_P3
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
#endif  //CS333_P3


#ifdef CS333_P3
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6

    acquire(&ptable.lock);  //get lock
    //Loop through processes list looking for process to run.
    p = ptable.list[RUNNABLE].head;
    while(p){
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
     
      //Remove from RUNNABLE list and add to RUNNING
      if(stateListRemove(&ptable.list[RUNNABLE], p) == -1)  
      {
        panic("stateListRemove() in scheduler() error (RUNNABLE -> RUNNING)");
      }
      assertState(p, RUNNABLE, __FUNCTION__, __LINE__);

      p->state = RUNNING;

      stateListAdd(&ptable.list[RUNNING], p);

#ifdef CS333_P2
      p->cpu_ticks_in = ticks;
#endif  //CS333_P2

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      p = p->next;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
 
#else //if not CS333_P3
//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

#ifdef CS333_P2
      p->cpu_ticks_in = ticks;
#endif  //CS333_P2

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif //CS333_P3

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
#ifdef CS333_P2
  p->cpu_ticks_total += (ticks - p->cpu_ticks_in);
#endif  //CS333_P2
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

#ifdef CS333_P3
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  //remove from RUNNING list and add to RUNNABLE
  if(stateListRemove(&ptable.list[RUNNING], curproc) == -1)
  {
    panic("stateListRemove() in yield() error\n");
  }
  assertState(curproc, RUNNING, __FUNCTION__, __LINE__);

  curproc->state = RUNNABLE;

  stateListAdd(&ptable.list[RUNNABLE], curproc);

  sched();
  release(&ptable.lock);
}

#else //if not CS333_P3
// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#endif //CS333_P3

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

#ifdef CS333_P3
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
 
  //remove from RUNNING list and add to SLEEPING
  if(stateListRemove(&ptable.list[RUNNING], p) == -1)
  {
    panic("stateListRemove() in sleep() error");
  }
  assertState(p, RUNNING, __FUNCTION__, __LINE__);

  p->state = SLEEPING;

  stateListAdd(&ptable.list[SLEEPING], p);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}

#else //if not CS333_P3
// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}
#endif //CS333_P3

#ifdef CS333_P3
static void
wakeup1(void *chan)
{
  struct proc *p;
  p = ptable.list[SLEEPING].head;
  while(p)
  {
    if(p->state == SLEEPING && p->chan == chan)
    {
      if(stateListRemove(&ptable.list[SLEEPING], p) == -1)
      {
        panic("stateListRemove() in wakeup1() error\n");
      }
      assertState(p, SLEEPING, __FUNCTION__, __LINE__);

      p->state = RUNNABLE;

      stateListAdd(&ptable.list[RUNNABLE], p);
    }
    p = p->next;
  } 
}

#else //if not CS333_P3
//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif //CS333_P3

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

#ifdef CS333_P3
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);

  for(int i = EMBRYO; i <= ZOMBIE; ++i)
  {
    p = ptable.list[i].head;
    while(p)
    {
      if(p->pid == pid)
      {
        p->killed = 1;
        // Wake process from sleep if necessary.
        if(p->state == SLEEPING)  //remove from SLEEPING list and add to RUNNABLE
        {
          if(stateListRemove(&ptable.list[SLEEPING],p) == -1)
          {
            panic("stateListRemove() in kill() error\n");
          }
          assertState(p ,SLEEPING ,__FUNCTION__ , __LINE__);

          p->state = RUNNABLE;

          stateListAdd(&ptable.list[RUNNABLE], p);
        }
        release(&ptable.lock);
        return 0;
      }
      else
        p = p->next;
    }
  }
  release(&ptable.lock);
  return -1;
}

#else //if not CS333_P3
// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#endif  //CS333_P3

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#if defined(CS333_P4)
void
procdumpP4(struct proc *p, char *state_string)
{
  cprintf("TODO for Project 4, deleete this line and implement procdrumpP4() in proc.c to print a row\n");
  return;
}
#elif defined(CS333_P3)
void
procdumpP3(struct proc *p, char *state_string)
{
  //elapsed time
  uint elapsed = ticks - p->start_ticks;//total elapsed ticks
  uint secs = elapsed/1000;//number of seconds
  uint tens = (elapsed %= 1000) /100;//tenths of a second
  uint hunds = (elapsed %= 100) /10;//hundredths of a second
  uint thous = (elapsed %= 10);//thousandths of a second

  //cpu time
  uint celapsed = p->cpu_ticks_total;
  uint csec = celapsed/1000;
  uint ctens = (celapsed %= 1000) /100;
  uint chunds = (celapsed %= 100) /10;
  uint cthous = (celapsed %= 10);

  int ppid = 0;
  if(!(p->parent))
    ppid = p->pid;
  else
    ppid = p->parent->pid;

  cprintf("%d\t%s\t     %d\t        %d\t%d\t%d.%d%d%d\t%d.%d%d%d\t%s\t%d\t",
      p->pid, p->name, p->uid, p->gid, ppid, secs, tens, hunds, thous, csec, ctens, chunds, cthous, state_string, p->sz );
  return;
}
#elif defined(CS333_P2)
void
procdumpP2(struct proc *p, char *state_string)
{
  //elapsed time
  uint elapsed = ticks - p->start_ticks;//total elapsed ticks
  uint secs = elapsed/1000;//number of seconds
  uint tens = (elapsed %= 1000) /100;//tenths of a second
  uint hunds = (elapsed %= 100) /10;//hundredths of a second
  uint thous = (elapsed %= 10);//thousandths of a second

  //cpu time
  uint celapsed = p->cpu_ticks_total;
  uint csec = celapsed/1000;
  uint ctens = (celapsed %= 1000) /100;
  uint chunds = (celapsed %= 100) /10;
  uint cthous = (celapsed %= 10);

  int ppid = 0;
  if(!(p->parent))
    ppid = p->pid;
  else
    ppid = p->parent->pid;
  
  cprintf("%d\t%s\t     %d\t        %d\t%d\t%d.%d%d%d\t%d.%d%d%d\t%s\t%d\t",
      p->pid, p->name, p->uid, p->gid, ppid, secs, tens, hunds, thous, csec, ctens, chunds, cthous, state_string, p->sz );
  return;
}
#elif defined(CS333_P1)
void
procdumpP1(struct proc *p, char *state_string)
{
  uint elapsed = ticks - p->start_ticks;//total elapsed ticks
  uint seconds = elapsed/1000;//number of seconds
  uint tenths = (elapsed %= 1000) /100;//tenths of a second
  uint hundredths = (elapsed %= 100) /10;//hundredths of a second
  uint thousandths = (elapsed %=10);//thousandths of a second
  
  cprintf("%d\t%s\t     %d.%d%d%d\t%s\t%d\t",
            p->pid, p->name, seconds,tenths, hundredths, thousandths, state_string, p->sz);
  return;
}
#endif

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

#if defined(CS333_P4)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P3)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName         Elapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif

  cprintf(HEADER);  // not conditionally compiled as must work in all project states

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";

    // see TODOs above this function
#if defined(CS333_P4)
    procdumpP4(p, state);
#elif defined(CS333_P3)
    procdumpP3(p, state);
#elif defined(CS333_P2)
    procdumpP2(p, state);
#elif defined(CS333_P1)
    procdumpP1(p, state);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
#ifdef CS333_P1
  cprintf("$ ");  // simulate shell prompt
#endif // CS333_P1
}

#if defined(CS333_P3)
// list management helper functions
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}
#endif

#if defined(CS333_P3)
static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    return -1;
  }

  struct proc* current = (*list).head;
  struct proc* previous = 0;

  if(current == p){
    (*list).head = ((*list).head)->next;
    // prevent tail remaining assigned when we've removed the only item
    // on the list
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found. return error
  if(current == NULL){
    return -1;
  }

  // Process found.
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  } else{
    previous->next = current->next;
  }

  // Make sure p->next doesn't point into the list.
  p->next = NULL;

  return 0;
}
#endif

#if defined(CS333_P3)
//hold ptable lock before using
static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#if defined(CS333_P4)
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif
}
#endif

#if defined(CS333_P3)
//hold ptable lock before using
static void
initFreeList(void)
{
  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; ++p){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}
#endif

#if defined(CS333_P3)
// example usage:
// assertState(p, UNUSED, __FUNCTION__, __LINE__);
// This code uses gcc preprocessor directives. For details, see
// https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html
static void
assertState(struct proc *p, enum procstate state, const char * func, int line)
{
    if (p->state == state)
      return;
    cprintf("Error: proc state is %s and should be %s.\nCalled from %s line %d\n",
        states[p->state], states[state], func, line);
    panic("Error: Process state incorrect in assertState()");
}
#endif

#ifdef CS333_P2
int
getprocs(uint max, struct uproc* table)
{
  int count = 0;
  struct proc* p; 

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(count < max)
    {
      if(strncmp(states[p->state],"runble", 6) == 0 || strncmp(states[p->state], "sleep", 5) == 0 || 
         strncmp(states[p->state], "run", 3) == 0 || strncmp(states[p->state], "zombie", 6) == 0)
      {
        table[count].pid = p->pid;
        table[count].uid = p->uid;
        table[count].gid = p->gid;
      
        if(!(p->parent))
          table[count].ppid = p->pid;
        else
          table[count].ppid = p->parent->pid;

        table[count].elapsed_ticks = ticks - p->start_ticks;
        table[count].CPU_total_ticks = p->cpu_ticks_total;
        safestrcpy(table[count].state, states[p->state], STRMAX);
        table[count].size = p->sz;
        safestrcpy(table[count].name, p->name, STRMAX);
        count++;
      }
    }
  }
  release(&ptable.lock);
  return count;
}
#endif  //CS333_P2

#ifdef CS333_P3
void
printRunnableList(void)
{
  acquire(&ptable.lock);
  cprintf("Ready List Processes:\n");
  if(ptable.list[RUNNABLE].head)  //check if list is not empty
  {
    struct proc* p = ptable.list[RUNNABLE].head;
    while(p)
    {
      cprintf("%d",p->pid);
      if(p->next)
      {
        cprintf("->");
        p = p->next;
      }
      else
        p = p->next;
    }
  }
  cprintf("\n$");
  release(&ptable.lock);
}

void
printNumUnused(void)
{
  acquire(&ptable.lock);
  struct proc* p = ptable.list[UNUSED].head;
  int count = 0;
  while(p)
  {
    count +=1;
    p = p->next;
  }
  cprintf("Free List Size: %d processes\n$",count);
  release(&ptable.lock);
}

void
printSleepingList(void)
{
  acquire(&ptable.lock);
  cprintf("Sleep List Processes:\n");
  if(ptable.list[SLEEPING].head)  //check if list is not empty
  {
    struct proc* p = ptable.list[SLEEPING].head;
    while(p)
    {   
      cprintf("%d",p->pid);
      if(p->next)
      {   
        cprintf("->");
        p = p->next;
      }    
      else
        p = p->next;
    }    
  }
  cprintf("\n$");
  release(&ptable.lock); 
}

void
printZombieList(void)
{
  acquire(&ptable.lock);
  cprintf("Zombie List Processes:\n");
  if(ptable.list[ZOMBIE].head)  //check if list is not empty
  {
    struct proc* p = ptable.list[ZOMBIE].head;
    while(p)
    {   
      cprintf("(%d,%d)",p->pid,p->parent->pid);
      if(p->next)
      {   
        cprintf("->");
        p = p->next;
      }    
      else
        p = p->next;
    }    
  }
  cprintf("\n$");
  release(&ptable.lock);
}

// from MM's xv6 P3 implementation. Linked to control-l in
// console.c. Students cannot assume that the code is correct
// and much validate it before using in tests.
void
printListStats()
{
  int i, count, total = 0;
  struct proc *p;

  acquire(&ptable.lock);
  for (i=UNUSED; i<=ZOMBIE; i++) {
    count = 0;
    p = ptable.list[i].head;
    while (p != NULL) {
      count++;
      p = p->next;
    }
    cprintf("\n%s list has ", states[i]);
    if (count < 10) cprintf(" ");  // line up columns
    cprintf("%d processes", count);
    total += count;
  }
  release(&ptable.lock);
  cprintf("\nTotal on lists is: %d. NPROC = %d. %s",
      total, NPROC, (total == NPROC) ? "Congratulations!" : "Bummer");
  cprintf("\n$ ");  // simulate shell prompt
  return;
}
#endif  //CS333_P3
