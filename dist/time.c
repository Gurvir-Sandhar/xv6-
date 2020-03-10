#ifdef CS333_P2

#include "types.h"
#include "user.h"

static void
print_time(uint milliseconds)
{ 
  uint elapsed = milliseconds;
  uint sec = elapsed/1000; //number of seconds
  uint tens = (elapsed %= 1000) /100; //tenths of a second
  uint hunds = (elapsed %= 100) /10; //hundredths of a second
  uint thous = (elapsed %= 10); //thousandths of a second

  printf(1,"%d.%d%d%d", sec, tens, hunds, thous);
  return;
}

int
main(int argc, char* argv[])
{
  if(argc <= 0)
  {
    exit();
  }

  uint start_time = uptime();
  int pid = fork();
  if(pid < 0)
  {
   printf(1,"fork failed in time.c");
   exit();
  } 
  else if(pid == 0) //work for child
  {
    exec(argv[1], argv+1);
    exit();
  }
  else //work for parent
  {

    pid = wait(); //wait for child to finish
    uint end_time = uptime();
    printf(1,"%s ran in ", argv[1]);
    print_time(end_time - start_time);
    printf(1," seconds.\n");
    exit();
  }
}

#endif  //CS333_P2
