#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void sig_handler(int signo) {
    char *s = strsignal(signo);
    printf("received signal. type: %s\n", s);
    if (signo == SIGINT) {
        printf("received SIGINT\n");
        exit(0);
    }
}

int main(void)
{
  if (signal(SIGINT, sig_handler) == SIG_ERR)
  printf("\ncan't catch SIGINT\n");
  // A long long wait so that we can easily issue a signal to this process
  while(1) 
    sleep(1);
  return 0;
}
