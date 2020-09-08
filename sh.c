#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <ctype.h>
#include <pthread.h>
#include <utmpx.h>
#include <fcntl.h>
#include "sh.h"

watch_t *watchUserList;
watch_t *watchMailList;
processThread_t *mailThreads;
int childProcesses = 0;
int stdin_save = -1;
int stdout_save = -1;
int stderr_save = -1;
int noclobber = 0;

int sh( int argc, char **argv, char **envp )
{
  char *prompt = calloc(PROMPTMAX, sizeof(char));
  char *commandline = calloc(MAX_CANON, sizeof(char));
  char *command, *arg, *commandpath, *p, *pwd, *owd;
  char **args = calloc(MAXARGS, sizeof(char*));
  char **aliasArray = malloc(10 * sizeof(char*));
  char **aliasCmdArray = malloc(10 * sizeof(char*));
  int uid, historyCounter = 0, status = 0, argsct = 1, go = 1, counter, aliasCounter = 0;
  struct passwd *password_entry;
  char *homedir;
  struct pathelement *pathlist;
  pid_t	pid;
  processThread_t watchThread;
  int mailCount = 1;
  int watchThreadRunning = 0;
  int piping = 0;
  int pipePid = 0;
  void *thread;

  char **commandhistory = calloc(10, sizeof(char*));

  /* Build a list of the built-in commands */
  char *builtInList[] = {"alias", "cd", "exit", "fg", "history", "kill", "list", "pid", "printenv", "prompt", "pwd", "setenv",
  "watchmail", "watchuser", "where", "which", '\0'};
  char *redirections[] = {">", ">&", ">>", ">>&", "<", '\0'};
  char *pipeList[] = {"|", "|&", '\0'};

  uid = getuid();
  password_entry = getpwuid(uid);               /* get passwd info */
  homedir = password_entry->pw_dir;		/* Home directory to start out with*/

  if ( (pwd = getcwd(NULL, PATH_MAX+1)) == NULL )
  {
    perror("getcwd");
    exit(2);
  }
  owd = calloc(strlen(pwd) + 1, sizeof(char));
  p = calloc(strlen(pwd) + 1, sizeof(char));
  memcpy(owd, pwd, strlen(pwd));
  memcpy(p, pwd, strlen(pwd));
  prompt[0] = ' '; prompt[1] = '\0';

  /* Put PATH into a linked list */
  pathlist = get_path();
  watchThread.running = 0;
  watchThread.created = 0;

  while ( go ) {

    if(pipePid > 0){
      waitpid(pipePid, NULL, 0);
    }
    else{
      /* check for zombies */
      waitpid(-1, &status, WNOHANG);
    }

    /* print the prompt */
    printf("%s [%s]> ", prompt, pwd);
    counter = 0;

    /* get the command line and process */
    if(fgets(commandline, 128, stdin) == NULL){
        printf("\n");
        continue;
    }
    int length = (int) strlen(commandline);
    commandline[length - 1] = '\0';

    addHistory(commandhistory, commandline, historyCounter);
    historyCounter++;

    /* dynamically allocate memory to the command history as it grows */
    if(historyCounter % 10 == 0){
        commandhistory = realloc(commandhistory, (historyCounter * 2) * sizeof(char*));
    }

    argsct = getArgs(commandline, arg, args);
    command = args[0];

    /*  this portion of the code will check all build-in commands first, then
        execute based on the supplied command.  Also checks if input/pipeOutput
        will be piped. If command is NULL, loop to next iteration of shell
    */
    if(command != NULL){

          /* search list of built-in commands */
          if(searchBuiltins(builtInList, command) == 0)
            printf("\nExecuting built-in command: %s\n\n", command);

          /* check to see if the command is a pipe */
          if(pipePid = createPipe(&commandpath, command, args, &argsct, pathlist, envp, pipeList)){
            piping = 1;
          }

          /* check for any aliases.  If found, change command */
          for(int i = 0; aliasArray[i] != NULL; i++){
            if(strcmp(command, aliasArray[i]) == 0)
                strcpy(command, aliasCmdArray[i]);
          }
          /* Calls exit and frees all memory */
          if (strcmp(command, "exit")==0){
             int j = 0;
              while(commandhistory[j] != NULL){

                free(commandhistory[j]);
                j++;
              }
              j = 0;
              while(aliasArray[j] != NULL){

                free(aliasArray[j]);
                free(aliasCmdArray[j]);
                j++;
              }
              j = 0;

              if(watchThread.created){
                freeWatchList(&watchUserList);
                printf("waiting for thread '%s' to exit.\n", watchThread.name);
                pthread_join(watchThread.thread, &thread);
              }
              freeWatchList(&watchMailList);
              freeProcessList(&mailThreads);
              freePathlist(pathlist);
              free(commandhistory);
              free(aliasArray);
              free(aliasCmdArray);
              free(prompt);
              free(args);
              free(commandline);
              free(p);
              free(owd);
              free(pwd);
              exit(0);
          }
          /* add command to alias table */
          else if(strcmp(command, "alias")==0){

              aliasCounter += addAlias(aliasArray, aliasCmdArray, args, aliasCounter);
          }
          /* changes directory */
          else if (strcmp(command, "cd")==0){

              cd(args, owd, &pwd, &p);
          }
          /* bring background process to foreground */
          else if (strcmp(command, "fg")==0){

              status = fg(args[1], argsct);
          }
          /* print command history */
          else if (strcmp(command, "history")==0){

              history(commandhistory, args[1]);
          }
          /* kill process */
          else if (strcmp(command, "kill")==0){

              mykill(args);
          }
          /* list files in given directory */
          else if (strcmp(command, "list")==0){

              /* if no arguments, list current directory */
              if(argsct == 1){
                list(pwd);
              }
              /* list every directory supplied by arguments */
              else{
                counter = 1;
                while(args[counter] != NULL){

                  printf("\n%s:\n",args[counter]);
                  list(args[counter]);
                  counter++;
                }
              }
          }
          /* return pid of current process */
          else if (strcmp(command, "pid")==0){

              pid = getpid();
              printf("pid: [%d]\n", pid);
          }
          /* print the environment */
          else if (strcmp(command, "printenv")==0){

              if(argsct > 2){
                printf("printenv: too many arguments\n");
              }
              else{
                printenv(envp, args[1]);
              }
          }
          /* change the prompt */
          else if (strcmp(command, "prompt")==0){

              myprompt(prompt, args[1]);
          }
          /* print working directory */
          else if (strcmp(command, "pwd")==0){

              printf("%s\n\n",pwd);
          }
          /* change variables in enrionment */
          else if (strcmp(command, "setenv")==0){

              if(argsct > 3){
                printf("setenv: too many arguments\n");
              }
              else{
                mysetenv(envp, args, &pathlist, &homedir);
              }
          }
          /* watch for mail in a given file */
          else if (strcmp(command, "watchmail") == 0){

            if(argsct < 2)
              printf("watchmail: Not enough arguments\n");

            else{
              char *path = malloc(sizeof(args[1]));
              strcpy(path, args[1]);
              if((strcmp(path, "off") != 0) && (access(path, X_OK) == 0)){
                printf("watchmail: file '%s' not found\n", path);
              }
              else{
                watchMail(&mailCount, args);
              }
                free(path);
            }
          }
          /* watched logged in users */
          else if (strcmp(command, "watchuser") == 0) {

            if(argsct < 2)
              printf("watchuser: Not enough arguments\n");
            else
              watchuser(&watchThread, args);
          }
          /* find all locations of executable */
          else if(strcmp(command, "where") == 0){

            where(args[1], pathlist);
          }
          /* find the location of an executable */
          else if (strcmp(command, "which")==0){

            counter = 1;

            if(argsct < 2){
              printf("which: Not enough arguments\n");
            }
            while(args[counter] != NULL){

              /* search built-in list for the command */
              if(searchBuiltins(builtInList, args[counter]) == 0){
                printf("%s: built-in command\n", args[counter]);
              }
              /* otherwise find the command path */
              else{
                commandpath = which(args[counter], pathlist);
                if(commandpath != NULL){
                  printf("%s\n", commandpath);
                  free(commandpath);
                }
              }
              counter++;
            }
          }
          /* set or unset noclobber */
          else if(argsct == 2 && strcmp(args[1],"noclobber") == 0){

            if(strcmp(args[0], "set") == 0){
              noclobber = 1;
            }
            else if(strcmp(args[0], "unset") == 0){
              noclobber = 0;
            }
          }
          /* check to see if the user supplied a redirection */
          else if(isRedirection(args, argsct, redirections)){

            stdout_save = dup(STDOUT_FILENO);
            stderr_save = dup(STDERR_FILENO);
            stdin_save = dup(STDIN_FILENO);

            if(redirection(&argsct, &args[argsct-2], &args[argsct-1])){
              status = execute(0, &commandpath, args[0], args, argsct, pathlist, envp, 1, NULL, NULL);
            }
          }
    /*  if the command is not one of the built-in commands above, the shell will
        execute the command if it can find it within the pathlist.  First
        it will check for any wildcards and execute the supplied commands
        based on an array of the matching filenames of the wildcard
    */
          else{
            counter = 0;
            int execFinished = 0;
            /* loop through each supplied argument string*/
            while(args[counter] != NULL){
              /* loop through each char in string to search for wildcards '*' or '?' */
              for(int j = 0; j < strlen(args[counter]); j++){

                if(args[counter][j] == '*' || args[counter][j] == '?'){

                    /* If either wildcard is found, create an array of matches */
                    glob_t path = wildcard(args[counter]);
                    path.gl_pathv[0] = args[0];
                    /* execute with match array as arg list */
                    status = execute(0, &commandpath, args[0], path.gl_pathv, argsct, pathlist, envp, 0, NULL, NULL);
                    execFinished = 1;

                    globfree(&path); /* free memory allocated for wildcards */
                }
              }
              counter++;
            }
            /* if there is no wildcard, execute as normal */
              if(execFinished != 1){
                  if(piping){ /* code to execute if piping */
                    status = execute(0, &commandpath, args[0], args, argsct, pathlist, envp, 1, NULL, NULL);
                    piping = 0;
                  }
                  else /* normal execution */
                    status = execute(0, &commandpath, args[0], args, argsct, pathlist, envp, 0, NULL, NULL);
              }
          }
    }
    /* restore shell to default stdin/out/err */
    if(piping){
      restoreShell();
      piping = 0;
    }

    /* clear memory of args and commandline */
    memset(args, 0, MAXARGS * sizeof(char*));
    memset(commandline, 0, MAX_CANON * sizeof(char));
  }
  return 0;
} /* sh() */

/* ============================== execute ================================
** Executes a command.
**
** Takes as arguments the pid, commandpath (which is updated within), the
** array of arguments, pathlist, and environment.  Forks the parent process
** and creates a new child process, and returns the status
**
** calls 'which' and allocates memory that is deallocated if chld does not execute
**
** no memory allocated that needs to be freed later
*/
int execute(int flag, char **commandpath, char* command, char **args, int argsct, struct pathelement *pathlist, char** envp, int redirect, int pipe[], char *pipeCommand){

    int childPid = -1;
    int parentPid = -1;
    int status = 0;
    int backgroundProcess = 0;

    /* if ending arg is &, start a new process thread */
    if(strcmp(args[argsct-1], "&") == 0){
      args[argsct-1] = '\0';
      backgroundProcess = 1;
    }


    if((childPid = fork()) < 0){
      perror("fork");
    }
    if (childPid == 0) {

      /* if redirect flag is set to 2, attach input piped */
      if(redirect == 2){

        pipeInput(pipeCommand, pipe);
      }
      /* get the path of the command from the pathlist */
      *commandpath = which(command, pathlist);

       //printf("\nExecuting: %s at path %s\n\n", command, *commandpath);

      /* execute and check for failure */
      if((status = execve(*commandpath, args, envp)) == -1){
        perror("execve");
        free(*commandpath);
        exit(status);
      }
    }
    else if (backgroundProcess == 1){
        childProcesses++;
        printf("[%d] %d\n", childProcesses, childPid);

    }
  	else if (redirect == 1){

      if((parentPid = waitpid(childPid, &status, 0)) < 0)
        perror("waitpid");

  			restoreShell();
    }
    else if (redirect == 2){

      pipeOutput(pipeCommand, pipe);
    }
    else {

      if((parentPid = waitpid(childPid, &status, 0)) < 0)
        perror("waitpid");
    }

    /* print the exit status */
    if (WIFEXITED(status)){
        if(status != 0)
        printf("\nChild's exit status: %d\n", WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status))
        printf("\nChild exited because of signal: %d\n", WTERMSIG(status));

    if(flag){
      return childPid;
    }
    else{
      return status;
    }
} /* end execute */

/* ============================== mykill ================================
** Mimics the kill command in the tcsh shell.  Will kill a pid given either
** the pid alone, which will send a signal of SIGTERM - or a signal is the
** format (-number) followed by the pid to kill
**
** Takes the array of aruments as an argument
**
** calls the 'checkDigits' function
**
** no memory allocated that needs to be freed later
*/
void mykill(char **args){

  int pid;
  int sig;
  int digit;
  char *substring;
  if(args[1] == NULL){
    printf("%s: Not enough arguments\n", args[0]);
  }
  /* loop through the array of aruments */
  for(int i = 1; args[i] != NULL; i++){

    /* make sure the command is numerical, can contain '-' */
    if(checkDigits(args[i]) != 0){

      printf("%s: Arguments must be of type int\n", args[0]);
    }
    else{
          /* if the command contains '-', include the user specified signal
          in the kill command */
          if(args[i][0] == '-' && args[i+1] != NULL){

            /* make sure the pid is not another signal */
            if(args[i+1][0] == '-'){
              printf("invalid pid (%s)\n", args[i+1]);
            }
            /* made sure the pid is an integer */
            else if (checkDigits(args[i+1]) != 0) {
              printf("%s: Arguments must be of type int\n", args[0]);
            }
            else{
              substring = strtok(args[i],"-");
              pid = atoi(args[i+1]);
              sig = atoi(substring);
              printf("killing pid: %d with signal: %d\n", pid, sig);
              kill(pid,sig);
              perror("kill");
            }
            i++;
          }
          /* if the command does not contain '-', send SIGTERM */
          else{
            pid = atoi(args[i]);
            printf("killing pid: %d with signal: %d\n", pid, SIGTERM);
            kill(pid,SIGTERM);
            perror("kill");
          }
    }
  }
} /* end kill */

/* ============================== cd ================================
** Mimics the cd command in the tcsh shell.  Will change the directory
** based on user input to the argument list.  If no arguments, go to
** the home directory.  Contains functionailty for '..', '/', and '-'.
**
** Takes as arguments the argument array, home directory, current
** working directory (which is updated inside), and the previous
** directory (which is updated inside).
**
** no memory allocated that needs to be freed later
*/
void cd(char **args, char *homedir, char **pwd, char **oldpwd)
{
  char *tmp;

  if(args[2] != NULL){
    //printf("%s: too many arguments\n", args[0]);
    return;
  }
  /* if no arguments, go to home directory and store current in oldpwd */
  if (args[1] == NULL) {
    chdir(homedir);
    strcpy(*oldpwd, *pwd);
    strcpy(*pwd, homedir);
  }
  /* if argument is '-', go to previous directory and store current in pwd */
  else if (strcmp(args[1],"-") == 0) {
    chdir(*oldpwd);

    tmp = malloc(sizeof(*pwd));
    strcpy(tmp, *pwd);
    strcpy(*pwd, *oldpwd);
    strcpy(*oldpwd, tmp);
    free(tmp);
  }
  /* change to directory specified. free pointer to the previous working directory
  to clear memory and gets the new current working directory with getcwd() */
  else {
    if (chdir(args[1]) != 0) {
      perror(args[1]);
    }
    else  {
      strcpy(*oldpwd, *pwd);
      free(*pwd);
      *pwd = getcwd(NULL, PATH_MAX+1);
    }
  }
}/* end cd */

/* ============================== which ================================
** Mimics the which command in the tcsh shell.  Will search for the given
** command in the pathlist and it's path.
**
** Takes as arguments the command and the pathlist
**
** allocates memory for the command that is returned.  Must be freed later
*/
char *which(char *command, struct pathelement *pathlist )
{
   char *cmd = malloc(MAXLINE * sizeof(char));

   if(command == NULL){
     strcpy(cmd,"Not enough arguments");
     return cmd;
   }
   /* if command begins with ./ treat it as a local executable, and attempt to
   access before returning */
   if((command[0] == '.' && command[1] == '/') || command[0] == '/'){

     if (access(command, X_OK) == 0)
        sprintf(cmd, "%s", command);
      else
        sprintf(cmd, "%s: could not access", command);

        return cmd;
   }
   /* search the pathlist for the command and return it's path */
   while (pathlist) {
     sprintf(cmd, "%s/%s", pathlist->element, command);
     if (access(cmd, X_OK) == 0)
        return cmd;

     pathlist = pathlist->next;
   }
   /* if command is not local exec and does not exit in pathlist, not found */
   sprintf(cmd, "'path not found'");
   return cmd;

} /* end which */

/* ============================== where ================================
** Mimics the where command in the tcsh shell.  Will search for all paths
** to the given command in the pathlist and print them.
**
** Takes as arguments the command and the pathlist
**
** allocates memory for a temporary string that is freed before exit
** no memory allocated that needs to be freed later
*/
void where(char *command, struct pathelement *pathlist)
{
   char *cmd = malloc(MAXLINE * sizeof(char));

   if(command == NULL){
     printf("Not enough arguments\n");
   }
   /* search the pathlist for the command and return it's path */
   while (pathlist) {
     sprintf(cmd, "%s/%s", pathlist->element, command);
     if (access(cmd, X_OK) == 0)
        printf("%s\n", cmd);

     pathlist = pathlist->next;
   }
   free(cmd);
} /* end where */

/* ============================== list ================================
** lists all of the files in the given directory
**
** Takes the directory as an argument
**
** no memory allocated that needs to be freed later
*/
void list ( char *dir )
{
  DIR *directory;
  struct dirent *dirEntry;

  if((directory = opendir(dir)) == NULL){

    perror("opendir");
    return;
  }
  while((dirEntry = readdir(directory)) != NULL)
      printf("%s\n", dirEntry->d_name);

    free(directory);
} /* end list */

/* ============================== myprompt ================================
** Allows the user to change the prefix of the commane prompt
**
** Takes as arguments the prompt (which is update inside), and a user suplied
** argument
**
** Allocates memory for a temporary variable that is freed before function exits
**
** no memory allocated that needs to be freed later
*/
void myprompt (char *prompt, char *arg){

  if(arg == NULL){
    char *tmp = malloc(PROMPTMAX * sizeof(char));
    printf("input prompt prefix: ");
    scanf("%s", tmp);
    strcpy(prompt, tmp);
    free(tmp);
  }
  else{
     strcpy(prompt, arg);
  }
} /* end myprompt */

/* ============================== printenv ================================
** Mimics the printenv function in tcsh.  prints the environment list
**
** Takes as arguments the environment and a single user-supplied argument
**
** no memory allocated that needs to be freed later
*/
void printenv (char **env, char *arg){

  if(arg == NULL){

    while(*env != NULL){

      printf("%s\n",*env++);
    }
  }
  else{
      char* tmp = getenv(arg);
      if(tmp == NULL)
        tmp = "Does not exist in environment";
      printf("%s\n", tmp);
  }
} /* end printenv */

/* ============================== mysetenv ================================
** Mimics the setenv command in tcsh.  Allows the user to change variables
** in the environment list.  If no arguments, prints the list.  If one
** argument, sets the specified variable to NULL.  If two arguments, sets
** the variable in args[1] to the value in args[2]
**
** Takes as arguments the environment, the argument list, the pathlist, and
** the home directory.
**
** frees the memory allocated for the previous pathlist if necessary by calling
** freePathlist();
** calls getPath() which allocates memory for a new pathlist
*/
void mysetenv (char **env, char **args, struct pathelement **pathlist, char **homedir){

  /* if no argument, print environment list */
  if(args[1] == NULL){
    printenv(env, args[1]);
    return;
  }
  /* if no second argument, set NULL to variable */
  else if(args[2] == NULL){

    if(setenv(args[1], "", 1) == -1)
      perror("setenv");

    /* if command was path, free the memory for the pathlist */
    if(strcmp(args[1],"PATH") == 0){

      freePathlist(*pathlist);
    }
  }
  else {
    /* if too many arguments, error */
    if(setenv(args[1], args[2], 1) == -1)
      perror("setenv");

    /* if variable is PATH, free memory to old path and create new */
    if(strcmp(args[1],"PATH") == 0){

      if(*pathlist == NULL)
        freePathlist(*pathlist);
      *pathlist = get_path();
    }
    /* if variable is HOME, change home directory */
    if(strcmp(args[1],"HOME") == 0){

      *homedir = args[2];
    }
  }

} /* end mysetenv */

/* ============================== history ================================
** Mimics the history command in tcsh.  If given no arguments, print out
** the entire command history.  If given a number argument, print the
** history up to that number
**
** Takes as arguments the command history (which is update inside) and a
** user specified number of items to print
**
** no memory allocated that needs to be freed later
*/
void history (char **history, char *num){

  if(num == NULL){
    int i = 0;
    while(*history != NULL){

      printf("%d: %s\n",++i, *history++);
    }
  }
  else{
    int number = atoi(num);

    for(int i = 0; i < number && history[i] != NULL; i++){

      printf("%d: %s\n", i, history[i]);
    }
  }
} /* end history */

/* ============================== addHistory ================================
** Adds the string stored in the commandline to the command history
**
** Takes as arguments the command history, commandline, and the counter for the
** history
**
** Allocates memory for a temporary variable that is added to the command
** history array.  Must be freed later
*/
void addHistory(char **commandhistory, char *commandline, int i){

  char *tmp = malloc((strlen(commandline)+1) * sizeof(char));
  strcpy(tmp, commandline);

  commandhistory[i] = tmp;
} /* end addHistory */

/* ============================== getArgs ================================
** Processes the arguments printed by the user on the commandline and stores
** them in an array
**
** Takes as arguments the command line, a temporary argument variable to
** delimit the strings, the argument list (which is updated inside),
** and a counter for the argument list
**
** no memory allocated that needs to be freed later
*/
int getArgs(char* commandline, char *arg, char **args){

  int argsct = 1;

  arg = strtok(commandline," ");
  args[0] = arg;

  while(arg != NULL){

    arg = strtok(NULL, " ");

    if(arg != NULL){
      args[argsct++] = arg;
    }
  }
  args[argsct] = '\0';

  return argsct;
} /* end getArgs */

/* ============================== searchBuiltins ================================
** Searches for the user specified command in the list of shell built-in commands
** returns an int value to indicate success or failure
**
** Takes as arguments the list of built in commands, and the command to search
**
** no memory allocated that needs to be freed later
*/
int searchBuiltins(char*builtInList[], char* command){
  int i = 0;
  while(builtInList[i] != NULL){

    if(command == NULL){
      return 1;
    }
    if(strcmp(builtInList[i],command) == 0)
      return 0;

    i++;
  }
  return 1;
} /* end searchBuiltins */

/* ============================== freePathlist ================================
** frees the memory for the pathlist
**
** Takes the pathlist as an argument
**
** no memory allocated that needs to be freed later
*/
void freePathlist(struct pathelement *pathlist){

  struct pathelement *current;

  free(pathlist->element);

  while(pathlist != NULL){

    current = pathlist;
    pathlist = pathlist->next;

    free(current);
  }
} /* end freePathlist */

/* ============================== wildcard ================================
** Uses the glob() command to search for files/directories specified by the
** given wildcards.  Returns glob_t structure with contains an array of all
** the matches
**
** Takes an argument which should contain wildcard values to search for
**
** glob() )allocates memory for an array of the matching paths.  Must be
** freed later
*/
glob_t wildcard(char *arg){

    glob_t  paths;
    int     csource;
    int       i = 0;

    paths.gl_offs = 1;
    csource = glob(arg, GLOB_DOOFFS|GLOB_NOCHECK, NULL, &paths);

    if (csource == 0) {

      char **p = paths.gl_pathv;
      while(*p != NULL){

        printf("%s\n", *p++);
      }
      return paths;
  }
} /* end wildcard */

/* ============================== checkDigits ================================
** Iterates through a string to see if any of the characters are not digits.
** returns an int value to indicate success or failure
**
** Takes the string as an argument
**
** no memory allocated that needs to be freed later
*/
int checkDigits(char* string){

  for(int i = 0; i < strlen(string); i++){

    if(isdigit(string[i]) == 0 && string[i] != '-'){
      return 1;
    }
  }
  return 0;
} /* end checkDigits */

/* ============================== addAlias ================================
** Mimics the tcsh alias command.  If no arguments, prints the current alias
** table.  Otherwise, takes 2 arguments and stores the new alias in the alias
** table.
**
** returns an int value to indicate success or failure, and to store in a
** counter to track array size.
**
** Takes as an argument the alias array, array of alias commands, the argument
** list, and the counter
**
** Allocates memory for two strings to store the alias and the command.  Must
** be freed later
*/
int addAlias(char **aliasArray, char **aliasCmdArray, char **args, int counter){

    char *alias;
    char *command;

    if(args[1] == NULL){

      for(int i = 0; aliasArray[i] != NULL; i++){
        printf("%d. Alias: %s\tCommand: %s\n",i+1,aliasArray[i],aliasCmdArray[i]);
      }
      return 0;
    }
    else if(args[2] == NULL){
      printf("%s: requires 2 arguments\n", args[0]);

      return 0;
    }
    else if(args[3] != NULL){
      printf("%s: too many arguments\n", args[0]);

      return 0;
    }
    else {

        alias = malloc(MAXLINE * sizeof(char));
        command = malloc(MAXLINE * sizeof(char));

        strcpy(alias,args[1]);
        strcpy(command,args[2]);

        aliasArray[counter] = alias;
        aliasCmdArray[counter] = command;

        return 1;
    }
} /* end addAlias */

/* ============================== watchuser ================================
** Watches a logged in user
**
** Takes as arguments a pointer to the process thread, and the array of arguments
**
** Creates a new process thread that calls the function startWatchingUsers()
** also adds and removes from the global linked list of watched getUsers
** If supplied an 'off' command in args[1], turns off the thread.  If
** supplied 'off' in args[2], removes the user in args[1] from the watchlist
**
** allocates memory for a process thread
*/
void watchuser(processThread_t *watchThread, char** args){
  /* turn off the thread */
  if(args[1] != NULL && strcmp(args[1], "off") == 0){

      watchThread->running = 0;
  }
  else{
      /* if the thread is not running, start the thread */
      if(watchThread->running == 0){
          if(pthread_create(&watchThread->thread, NULL, startWatchingUsers, watchThread) != 0){
              perror("pthread_create");
          }
          else{
            watchThread->created = 1;
            watchThread->running = 1;
            watchThread->name = "watchuser";
          }
      }
      else{
        watchThread->running = 1;
      }
      /* add or remove user to the watchlist */
      if(args[2] != NULL && strcmp(args[2], "off") == 0){
          removeFromWatchList(args[1], &watchUserList);
      }
      else{
          addToWatchList(args[1], &watchUserList);
      }
  }
} /* end watchuser */

/* ============================== startWatchingUsers ===========================
** Starts the process thread to watch users
**
** Takes as arguments a pointer to the process thread.
**
** The processthread->running variable is used to determine whether or not to
** continue the while() loop, which will end of watchThread->running is update
** to become 0 from watchuser() or getUsers()
**
** Calls getUsers().
**
** no memory allocated
*/
void *startWatchingUsers(void *arg){

  processThread_t *watchThread = (processThread_t*)arg;
  while(watchThread->running){

    watchThread->running = getUsers();
    if(watchThread->running != 0)
    sleep(3);
  }
  pthread_exit(NULL);
} /* end startWatchingUsers */

/* ============================== watchMail ================================
** Watches a file to see if it has grown larger over time.  Creates multiple
** threads to watch each file supplied as an argument
**
** Takes as arguments a pointer to an array of process threads, a pointer to a
** counter for the number of threads, and the array of arguments
**
** Creates a new process thread that calls the function startWatchingMail()
** Adds and removes from the global linked list of watched files
** If supplied an 'off' command in args[1], turns off every thread.  If
** supplied 'off' in args[2], removes the file in args[1] from the watchlist
**
** allocates memory for a process thread
*/
void watchMail(int *threadCount, char** args){

  int exists = 0;
  watch_t *current = watchMailList;
  processThread_t *tmp;

  if(args[1] != NULL && strcmp(args[1], "off") == 0){

    /*  for(int i = 0; i <= *threadCount; i++){
        watchThreads[i]->running = 0;
      }*/
  }
  else{
      tmp = addToProcessList(args[1], &mailThreads);

          if(pthread_create(&tmp->thread, NULL, startWatchingMail, tmp) != 0){
              perror("pthread_create");
          }
          else{
            tmp->running = 1;
            tmp->created = 1;
            *threadCount += 1;
          }
      if(args[2] != NULL && strcmp(args[2], "off") == 0){
          removeFromWatchList(args[1], &watchMailList);
          removeFromProcessList(args[1], &mailThreads);
      }
      else{
          if(exists != 1)
          addToWatchList(args[1], &watchMailList);
      }
  }
} /* end watchMail */

/* ============================== startWatchingMail ===========================
** Starts the process thread to watch a file
**
** Takes as arguments a pointer to the process thread.
**
** The watchThread->running variable is used to determine whether or not to
** continue the while() loop, which will end ff watchThread->running is updated
** to become 0 from watchMail() or geMail()
**
** Calls getMail().
**
** no memory allocated
*/
void *startWatchingMail(void *arg){

    processThread_t *watchThread = (processThread_t*)arg;
    while(watchThread->running){

    watchThread->running = getMail(watchThread);
    if(watchThread->running != 0)
    sleep(1);
  }

} /* end startWatchingMail */

/* ============================== getUsers ===========================
** Searches the global list of login records for a user that is being
** watched.  If a user in the list of watched users is found, print
** the current login info about the user.
**
** If a user in the watch list is found, return a value of 1 to signal
** startWatchingUsers to continue running the thread.  If no users are
** found, return 0 which will signal the thread to end
**
** no memory allocated
*/
int getUsers(){

  struct utmpx *up;
  int found = 0;
  setutxent();			/* start at beginning */
  while (up = getutxent())	/* get an entry */
  {
     if ( up->ut_type == USER_PROCESS )	/* only care about users */
     {
       watch_t *current = watchUserList;

       while(current != NULL){

         if(strcmp(up->ut_user, current->name) == 0){
           current->entry = up;
           printf("\n%s is logged on %s from %s\n", current->entry->ut_user, current->entry->ut_line, current->entry->ut_host);
           found = 1;
         }
         current = current->next;
       }
     }
  }
  if(found == 1)
    return 1;
  else
    return 0;
} /* end getUsers */

/* ============================== getMail ===========================
** Iteraltes through the global linked list of files to see if the
** size of the file has changed.  If the file size is larger than the
** previously saved file size, inform the user
**
** Takes as arguments a pointer to the thread
**
** If a file in the watch list is found that matches the name of the thread,
** return a value of 1 to signal startWatchingMail to continue running the thread.
** If no users are found, return 0 which will signal the thread to end
**
** no memory allocated
*/
int getMail(processThread_t *watchThread){

    int found = 0;
    struct timeval currentTime;
    struct stat buf;

    gettimeofday(&currentTime, NULL);

    watch_t *current = watchMailList;

    while(current != NULL){

      if(strcmp(current->name, watchThread->name) == 0){
        stat(current->name, &buf);
        if(current->size < buf.st_size){
          printf("\nYou've got mail!\nIn mailbox: %s\nAt time: %s\a\n", current->name, ctime(&currentTime.tv_sec));
          current->size = buf.st_size;
        }
        found = 1;
        break;
      }
      current = current->next;
    }
    if(found == 1)
      return 1;
    else
      return 0;
} /* end getMail */

void addToWatchList(char* entry, watch_t **watchList){

  watch_t *tmp = (watch_t*)malloc(sizeof(watch_t));

  tmp->name = calloc(MAXLINE, sizeof(char));
  strcpy(tmp->name, entry);
  if(*watchList == NULL){
      *watchList = tmp;
  }
  else{
    watch_t *current = *watchList;
    /*iterate through list */
    while (current->next != NULL) {

        current = current->next;
    }
    current->next = tmp;
    tmp->previous = current;
  }
}
void removeFromWatchList(char* entry, watch_t **watchList){

  watch_t *current = *watchList;
  watch_t *tmp;
  /*iterate through list */
  while (current != NULL) {

      if(strcmp(current->name,entry) == 0){

        if(*watchList == NULL || current == NULL)
          return;

        if(*watchList == current)
          *watchList = current->next;

        if(current->next != NULL)
          current->next->previous = current->previous;

        if(current->previous != NULL)
          current->previous->next = current->next;

        free(current->name);
        free(current);
      }
        current = current->next;
  }
}
void freeWatchList(watch_t **watchList){

  watch_t *current;

  while(*watchList != NULL){

    current = *watchList;
    *watchList = (*watchList)->next;

    free(current->name);
    free(current);
  }
}
processThread_t *addToProcessList(char* entry, processThread_t **pList){

  processThread_t *tmp = (processThread_t*)malloc(sizeof(processThread_t));

  tmp->name = calloc(MAXLINE, sizeof(char));
  strcpy(tmp->name, entry);
  if(*pList == NULL){
      *pList = tmp;
  }
  else{
    processThread_t *current = *pList;
    /*iterate through list */
    while (current->next != NULL) {

        current = current->next;
    }
    current->next = tmp;
    tmp->previous = current;
  }
  return tmp;
}
void removeFromProcessList(char* entry, processThread_t **pList){

  processThread_t *current = *pList;
  processThread_t *tmp;
  void *thread;

  while (current != NULL) {

      if(strcmp(current->name,entry) == 0){

        if(*pList == NULL || current == NULL)
          return;

        if(*pList == current)
          *pList = current->next;

        if(current->next != NULL)
          current->next->previous = current->previous;

        if(current->previous != NULL)
          current->previous->next = current->next;

        current->running = 0;
        printf("waiting for thread '%s' to exit.\n", current->name);
        free(current->name);
        pthread_join(current->thread, &thread);
      }
        current = current->next;
  }
}
void freeProcessList(processThread_t **pList){

  processThread_t *current;
  void *thread;

  while(*pList != NULL){

    current = *pList;
    *pList = (*pList)->next;

    printf("waiting for thread '%s' to exit.\n", current->name);
    free(current->name);
    pthread_join(current->thread, &thread);
    free(current);
  }
}
int isRedirection(char **args, int argsct, char *redirections[]){

    int index = checkRedirection(redirections, args);
    if(argsct >= 3 && index != 0)
      return index;
    else{
        return 0;
    }
}
int checkRedirection (char *redirections[], char **args){

  for(int index = 0; args[index] != NULL; index++){

    for(int i = 0; redirections[i] != NULL; i++){

      if(strcmp(redirections[i], args[index]) == 0)
          return index;
    }
  }
  return 0;
}
int redirection (int *argsct, char **redirect, char **rightOperand){

  int file;

  if(access(*rightOperand, X_OK) == 0 && noclobber == 1){
    printf("redirection: cannot overwrite file\n");
    return 0;
  }

	if(strcmp(*redirect,">") == 0){

      close(STDOUT_FILENO);
      file = open(*rightOperand, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
      file = dup(STDOUT_FILENO);
	}
	else if(strcmp(*redirect,">>") == 0){

      close(STDOUT_FILENO);
      file = open(*rightOperand, O_CREAT|O_WRONLY|O_APPEND, S_IRWXU);
      file = dup(STDOUT_FILENO);
	}
	else if(strcmp(*redirect,">&") == 0){

      close(STDOUT_FILENO);
      close(STDERR_FILENO);
      file = open(*rightOperand, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
      file = dup(STDOUT_FILENO);
      file = dup(STDERR_FILENO);
	}
  else if(strcmp(*redirect,">>&") == 0){

      close(STDOUT_FILENO);
      close(STDERR_FILENO);
      file = open(*rightOperand, O_CREAT|O_WRONLY|O_APPEND, S_IRWXU);
      file = dup(STDOUT_FILENO);
      file = dup(STDERR_FILENO);
  }
  else if(strcmp(*redirect,"<") == 0){

      close(STDIN_FILENO);
      file = open(*rightOperand, O_RDONLY, S_IRWXU);
      file = dup(STDIN_FILENO);
  }
  close(file);

  *rightOperand = '\0';
  *redirect = '\0';
  *argsct -= 2;

  return 1;
}
int createPipe(char **commandpath, char* command, char **args, int *argsct, struct pathelement *pathlist, char **envp, char *pipeList[]){

    int pipeIndex;
    int childPid;
    if((pipeIndex = isRedirection(args, *argsct, pipeList)) != 0){

      char **newArgs = calloc(MAXARGS, sizeof(char*));
      int newArgsCt = 0;
      int fd[2];
      char *pipeCommand = calloc(MAXLINE, sizeof(char));

      pipe(fd);
      stdin_save = dup(STDIN_FILENO);
      stdout_save = dup(STDOUT_FILENO);
      stderr_save = dup(STDERR_FILENO);

      strcpy(pipeCommand, args[pipeIndex]);
      args[pipeIndex] = '\0';
      pipeIndex++;
      (*argsct)--;

      for(int i = 0; args[pipeIndex] != NULL; i++){

        newArgs[i] = args[pipeIndex];
        args[pipeIndex] = '\0';
        pipeIndex++;
        newArgsCt++;
        (*argsct)--;
      }

      childPid = execute(1, commandpath, newArgs[0], newArgs, newArgsCt, pathlist, envp, 2, fd, pipeCommand);
      close(fd[0]);

      memset(newArgs, 0, MAXARGS * sizeof(char*));
      free(pipeCommand);
      free(newArgs);

      return childPid;
  }
  else
    return 0;
}
void pipeInput(char *command, int pipe[]){

  if(!strcmp(command, "|")){

    close(STDIN_FILENO);
    dup(pipe[0]);
    close(pipe[0]);
    close(pipe[1]);
  }
  else if(!strcmp(command, "|&")){

    close(STDIN_FILENO);
    dup(pipe[0]);
    close(pipe[0]);
    close(pipe[1]);
  }
}
void pipeOutput(char *command, int pipe[]){

  if(strcmp(command, "|")){

    close(STDOUT_FILENO);
    dup(pipe[1]);
    close(pipe[1]);
    close(pipe[0]);
  }
  else if(strcmp(command, "|&")){

    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    dup(pipe[1]);
    dup(pipe[1]);
    close(pipe[1]);
    close(pipe[0]);
  }
}
void restoreShell(){

  close(STDIN_FILENO);
  dup(stdin_save);
  close(stdin_save);

  close(STDOUT_FILENO);
  dup(stdout_save);
  close(stdout_save);

  close(STDERR_FILENO);
  dup(stderr_save);
  close(stderr_save);
}
int fg (char *arg, int argsct){

  int pid;
  int status;

  if(argsct > 2){
    printf("fg: Too many arguments\n");
  }
  if(arg != NULL){

    if(checkDigits(arg) == 1){
      printf("fg: pid must be an integer\n");
    }
    else{
      pid = atoi(arg);
      waitpid(pid, &status, 0);
      return status;
    }
  }
  else{
    waitpid(0, &status, 0);
    return status;
  }
  return -1;
}
