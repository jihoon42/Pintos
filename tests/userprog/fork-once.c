/* Forks and waits for a single child process. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int pid = 10;
  printf("Initial pid: %d\n", pid);

  if ((pid = fork("child"))){
    printf("Child pid in Parent(if): %d\n", pid);
    int status = wait (pid);
    msg ("Parent: child exit status is %d", status);
  } else {
    printf("Child pid in else: %d\n", pid);
    msg ("child run");
    exit(81);
  }
}
