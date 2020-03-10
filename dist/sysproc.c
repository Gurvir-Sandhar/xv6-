#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#ifdef PDX_XV6
#include "pdx-kernel.h"
#endif // PDX_XV6

#ifdef CS333_P2
#include "uproc.h"
#endif  //CS333_P2

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
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

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      return -1;
    }
    sleep(&ticks, (struct spinlock *)0);
  }
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  xticks = ticks;
  return xticks;
}

#ifdef PDX_XV6
// shutdown QEMU
int
sys_halt(void)
{
  do_shutdown();  // never returns
  return 0;
}
#endif // PDX_XV6

#ifdef CS333_P1
int
sys_date(void)
{
  struct rtcdate *d;

  if(argptr(0, (void*)&d, sizeof(struct rtcdate)) < 0)
      return -1;
  else{
    cmostime(d);
    return 0;
  }
}
#endif  //CS333_P1

#ifdef CS333_P2
int
sys_getuid(void)
{
  return myproc()->uid;
}

int 
sys_getgid(void)
{
  return myproc()->gid;
}

int
sys_getppid(void)
{
  if(!(myproc()->parent)) //if parent is null
    return myproc()->pid;
  else
    return myproc()->parent->pid;
}

int
sys_setuid(void)
{
  int n;
  
  if(argint(0, &n) < 0)
    return -1;
  if(n < 0 || n > 32767)
    return -1;

  myproc()->uid = n;
  return 0;
}

int 
sys_setgid(void)
{
  int n;

  if(argint(0, &n) < 0)
    return -1;
  if(n < 0 || n > 32767)
    return -1;

  myproc()->gid = n;
  return 0;
}

int
sys_getprocs(void)
{
 int n;
 struct uproc* up;

 if(argint(0,&n) < 0)
   return -1;
 if(argptr(1,(void*)&up, sizeof(struct uproc[n])) < 0)
   return -1;

 int num = getprocs(n,up);
 return num;
}
#endif  //CS333_P2

#ifdef CS333_P4
int 
sys_setpriority(void)
{
  int pid;
  int priority;

  if(argint(0, &pid) < 0)
    return -1;
  if(argint(1, &priority) < 0)
    return -1;
  if(priority < 0 || priority > MAXPRIO)
    return -1;
  
  return setprio(pid, priority);
}

int 
sys_getpriority(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;

  return getprio(pid);
}
#endif  //CS333_P4
