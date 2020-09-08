/* Jason Nichols-Allen
** CISC361
** Project 2
**
** This program creates a shell that mimics some functions
** of tcsh
*/

#include "sh.h"
#include <signal.h>
#include <stdio.h>

void sig_handler(int sig);

int main( int argc, char **argv, char **envp )
{

  //signal(SIGINT, sig_handler);
  //signal(SIGTERM, sig_handler);
  //signal(SIGTSTP, sig_handler);

  return sh(argc, argv, envp);
}
void sig_handler(int sig)
{
  if(sig == SIGINT) return;
  else if(sig == SIGTERM) return;
  else if(sig == SIGTSTP) return;
}
