#ifdef CS333_P2

#include "types.h"
#include "user.h"
#include "uproc.h"

static void
print_time(uint milliseconds)
{ 
  uint elapsed = milliseconds;
  uint sec = elapsed/1000; //number of seconds
  uint tens = (elapsed %= 1000) /100; //tenths of a second
  uint hunds = (elapsed %= 100) /10; //hundredths of a second
  uint thous = (elapsed %= 10); //thousandths of a second

  printf(1,"\t%d.%d%d%d", sec, tens, hunds, thous);
  return;
}

int 
main(int argc, char* argv[])
{
  int max = 72;
  struct uproc* table = malloc(sizeof(struct uproc[max]));
  int num_procs = 0;
  num_procs = getprocs(max, table);

  if(num_procs == -1)//error
  {   
    printf(1,"getprocs error in ps.c\n");
    free(table);
    exit();
  }
  else if(num_procs == 0)//no processes
  {
    printf(1,"no processes returned from getproc");
    free(table);
    exit();
  }
  else
  {
    printf(1,"%s\t%s          %s\t%s\t%s\t%s\t  %s\t  %s\t%s\t\n",
          "PID", "Name", "UID", "GID", "PPID", "Elapsed", "CPU","State", "Size");
    for(int i = 0; i < num_procs; i++)
    {
      printf(1,"%d\t%s\t\t%d\t%d\t%d", table[i].pid, table[i].name, table[i].uid, table[i].gid
          ,table[i].ppid);
      print_time(table[i].elapsed_ticks);//elapsed time
      print_time(table[i].CPU_total_ticks);//cpu time
      printf(1,"\t%s\t%d\n", table[i].state, table[i].size);
    }
  }
  free(table);
  exit();
}

#endif  //CS333_P2
