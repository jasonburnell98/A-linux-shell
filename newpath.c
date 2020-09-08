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
#include <signal.h>
#include "shell.h"
#include <glob.h>
#include <sys/stat.h>

int sh(int argc, char **argv, char **envp){
  char *prompt = calloc(PROMPTMAX, sizeof(char));
  char *commandline = calloc(MAX_CANON, sizeof(char));
  char *command, *arg, *commandpath, *p, *pwd, *owd;
  char **args = calloc(MAXARGS+1, sizeof(char*));
int uid, i, status, argsct, go = 1;
struct passwd *password_entry;
char *homedir;
struct pathelement *pathlist;
char prevDir[PATH_MAX], cwd[PATH_MAX];

uid = getuid();
password_entry = getpwuid(uid);               /* get passwd info */
homedir = password_entry->pw_dir;/* Home directory to start
                                    out with*/

if ((pwd = getcwd(NULL, PATH_MAX+1)) == NULL){
  perror("getcwd");
  exit(2);
}

owd = calloc(strlen(pwd) + 1, sizeof(char));
memcpy(owd, pwd, strlen(pwd));
prompt[0] = ' '; prompt[1] = '\0';

/* Put PATH into a linked list */
pathlist = get_path();

// Process ID of the shell
pid = getpid();

while(go){
  /* print your prompt */
  getcwd(cwd, PATH_MAX);
  printf("%s [%s]> ", prompt, cwd);

  /* get command line and process */
  if(fgets(commandline, MAX_CANON, stdin) != NULL){
    commandline[strlen(commandline)-1] = '\0';
    int numArgs = 0;
    char* token = strtok(commandline, " ");

    command = token;
    args[numArgs++] = token;
    token = strtok(NULL, " ");

    /*
      Extract all of the arguments for the command, allocating memory in the args matrix each time
we come across a new command.
*/
while(token && numArgs < MAXARGS+1){
args[numArgs++] = token;
}
token = strtok(NULL, " ");

if(command){

if(strcmp(command, "exit") == 0){
  exitProtocol(prompt, commandline, args, owd, pwd, pathlist);
  exit(0);
}

/* check for each built in command and implement */
else if(strcmp(command, "which") == 0){
  int argCount = 1;

  while(argCount < MAXARGS+1 && args[argCount]){
    char* result = which(args[argCount++], pathlist);
    if(result){
      printf("%s \n", result);
      free(result);
    }
  }
}

else if(strcmp(command, "where") == 0){
  int argCount = 1;

  while(argCount < MAXARGS+1 && args[argCount]){
    where(args[argCount++], pathlist);
  }
}

else if(strcmp(command, "pwd") == 0){
  gwd(cwd);
}

else if(strcmp(command, "prompt") == 0){
  if(args[1]){
    changePrompt(prompt, args[1]);
  }
else{
  char newPrefix[PROMPTMAX];
  printf("input prompt prefix: ");
  fgets(newPrefix, PROMPTMAX, stdin);
  newPrefix[strlen(newPrefix)-1] = '\0';
  changePrompt(prompt, newPrefix);
}
}


else if(strcmp(command, "cd") == 0){
if((numArgs-1) > 1){
  fprintf(stderr, "too many arguments \n");
}
else{
  cd(args[1], prevDir);
}
}

else if(strcmp(command, "pid") == 0){
printf("%d \n", pid);
}

else if(strcmp(command, "kill") == 0){
// We have a signal to send
if(args[1] && args[2]){
  int processID = atoi(args[2]);
  /* There is a '-' before the signal, so we can skip it by adding 1 to the address
     of args[1] to just get the signal itself */
  int signal = atoi(args[1]+1);

  if(processID = pid){
    exitProtocol(prompt, commandline, args, owd, pwd, pathlist);
  }

  killProcess(processID, signal);
}

// We dont have a signal to send
else if(args[1]){
  int processID = atoi(args[1]);
  killProcess(processID, SIGTERM);
}
}

else if(strcmp(command, "list") == 0){
  if((numArgs-1) == 0){
    char currentDir[PATH_MAX];
    list(currentDir);
    gwd(currentDir);
  }

  else if((numArgs-1) >= 1){
    int currentArg = 1;

    while(currentArg < MAXARGS+1 && args[currentArg]){
      printf("\n");
      printf("%s: \n", args[currentArg]);
      list(args[currentArg++]);
    }
  }
}

else if(strcmp(command, "printenv") == 0){
  if((numArgs-1) == 0){
    printenv(envp);
  }

  else if((numArgs-1) == 1){
    if(getenv(args[1])){
      printf("%s \n", getenv(args[1]));
    }
  }

  else{
    fprintf(stderr, "too many arguments \n");
  }
}

else if(strcmp(command, "setenv") == 0)
{
  if((numArgs-1) == 0){
    printenv(envp);
  }

else if((numArgs-1) <= 2){
  char* env = args[1];
  char* envValue = args[2] == NULL ? "" : args[2];
  setenvironvar(env, envValue);
}




// Given a path, lets try to execute it given its a file
else if(command[0] == '/' || command[0] == '.'){
if(isFile(command) && (access(command, F_OK) == 0) && (access(command, X_OK) == 0)){
  int forkid = fork();
  if(!forkid){
    execv(command, args);
    fprintf(stderr, "failed to execute %s \n", command);
    exitProtocol(prompt, commandline, args, owd, pwd, pathlist);
    exit(0);
  }

  else{
    int rc_wait = waitpid(forkid, NULL, 0);
  }
}
else{
  fprintf(stderr, "%s is either a directory or could not be found \n", command);
}
}

/*  else  program to exec */
else {
char* result = which(command, pathlist);
char** newArgs = expandWildcard(args, numArgs);

// If which returned a path to the given command, we can execute it
if(result){
  int forkid = fork();                                                                                                                                                   if(!forkid){                                                                                                                                                             execv(result,newArgs);                                                                                                                                                fprintf(stderr, "failed to execute command %s \n", command);
    exitProtocol(prompt, commandline, args, owd, pwd, pathlist);
exit(0);
}

else{
int rc_wait = waitpid(forkid, NULL, 0);
}
}

else{
fprintf(stderr, "%s: Command not found.\n", command);
}

free(result);
free(newArgs);
}

// Clear all args in args array for next iteration
clearArgs(args);
command = 0;
}
}

// For ctrl-d, just print a new line instead of crashing
else{
printf("\n");
}
}
return 0;
} /* sh() */


/*
Find and return the instance of a command by iterating through the path list linked list.
Allocates memory for the path concatenated with the command name, which is freed upon an
unsuccessful search but must be freed outside of the function if it successfuly returns.
*/
char *which(char *command, struct  pathelement *pathlist){
/* loop through pathlist until finding command and return it.  Return
NULL when not found. */                                                                                                                                             struct pathelement *current = pathlist;                                                                                                                                                                                                                                                                                                       while(current){
char* desiredPath = malloc(strlen(current->element) + strlen(command) + 2);
strcpy(desiredPath, current->element);
strcat(desiredPath, "/");
strcat(desiredPath, command);

if(access(desiredPath, F_OK) == 0){
  return desiredPath;
}

free(desiredPath);
current = current->next;
}

return NULL;
} /* which() */


/*
Finds all instances of a command by iterating through the path list linkedlist
Allocates memory to hold the path concatenated with the command, and frees after
every iteration.
*/
char *where(char *command, struct pathelement *pathlist){
/* similarly loop through finding all locations of command */
struct pathelement *current = pathlist;

while(current){
char* desiredPath = malloc(strlen(current->element) + strlen(command) + 2);
strcpy(desiredPath, current->element);
strcat(desiredPath, "/");
strcat(desiredPath, command);

if(access(desiredPath, F_OK) == 0){
  printf("%s \n", desiredPath);
}

free(desiredPath);
current = current->next;
}
                                                                                                                                                                     return NULL;                                                                                                                                                                                                                                                                                                                                } /* where() */
/*
  Change directory from the current directory to either a given directory, the home directory if there is
  no specified directory, or to the previous directory using the chdir system call.
 */
void cd(char* command, char* prevDir){
  char currentDir[PATH_MAX];
  getcwd(currentDir, PATH_MAX);

  if(!command){
    // Store the current directory as the previous directory
    strcpy(prevDir, currentDir);
    chdir(getenv("HOME"));
  }

  else if(strcmp(command, "-") == 0){
    // Store the current directory as the previous directory
    chdir(prevDir);
    strcpy(prevDir, currentDir);
  }

  else if(chdir(command) == 0){
    // Update the previous directory because we successfully
    // changed directories
    strcpy(prevDir, currentDir);
  }

  else{
    perror("could not change directory");
  }
}

void gwd(char* cwd){
  getcwd(cwd, PATH_MAX);

  if(cwd != NULL){
    printf("%s \n", cwd);
  }

  else{
      perror("gwd() error");
   }
          }
/*
  Given a directory by the user, iterate through the contents of the directory and print them.
  If no directory is specified, the contents of the current directory are listed.
 */

struct pathelement *get_path(){
  /* path is a copy of the PATH and p is a temp pointer */
  char *path, *p;

  /* tmp is a temp point used to create a linked list and pathlist is a
     pointer to the head of the list */
  struct pathelement *tmp, *pathlist = NULL;

  p = getenv("PATH");/* get a pointer to the PATH env var.
                           make a copy of it, since strtok modifies the
                           string that it is working with... */
  path = malloc((strlen(p)+1)*sizeof(char));/* use malloc(3) */
  strncpy(path, p, strlen(p));
  path[strlen(p)] = '\0';

  p = strtok(path, ":"); /* PATH is : delimited */
  do{/* loop through the PATH */
    /* to build a linked list of dirs */
    if (!pathlist){/* create head of list */                                                                                                                                 tmp = calloc(1, sizeof(struct pathelement));                                                                                                                             pathlist = tmp;                                                                                                                                                    }
    else{/* add on next element */
tmp->next = calloc(1, sizeof(struct pathelement));
tmp = tmp->next;
}
tmp->element = p;
tmp->next = NULL;
} while (p = strtok(NULL, ":"));

return pathlist;
}

void sig_handler(int signal){}

/*
Set all of the values in the args matrix to null, so that they
do not carry over in the next command entered by the user in the shell.
*/
void clearArgs(char** args){
for(int i = 0; i < MAXARGS+1; i++){
args[i] = 0;
}
}

/*
Given a new prefix, replace the old prompt prefix with it.
*/
void changePrompt(char* prompt, char* prefix){
strcpy(prompt, prefix);
}

/*
Free the allocated memory done in setup
*/
void cleanup(char* prompt, char* commandline, char** args, char* owd, char* pwd){
free(prompt);
free(commandline);
free(owd);
free(pwd);
free(args);
}                                                                                                                                                                                                                                                                                                                                             /*                                                                                                                                                                       Free the memory allocated by the pathlist linkedlist.
*/
void cleanupPathlist(struct pathelement *pathlist){
  struct pathelement *current = pathlist;
  free(current->element);
  while(current){
    struct pathelement *next = current->next;
    free(current);
    current = next;
  }
}

/*
  Given a process id and a signal, kill the process and send the signal.
  If the user does not give a signal, SIGTERM is sent by default.
 */
void killProcess(int pid, int signal){
  if(!(kill(pid, signal) == 0)){
    perror("could not kill process");
  }
}

void printenv(char** envp){
  int currentEnv = 0;

  while(envp[currentEnv]){
    printf("%s \n", envp[currentEnv++]);
  }
}

/*
  Uses the setenv system call to set the given evironment variable to the given value.
  If the user does not specify a value to set the environment variable to, it is set to the empty string ""
 */
void setenvironvar(char* env, char* envVal){
  if(setenv(env, envVal, 1)){
    perror("could not set environment");
  }
}

                                                                                                                                                                       /*                                                                                                                                                                       Return the index of the argument with a wildcard if the args passed in by the user contains a wildcard * or ?                                                          Return 0 otherwise because we know the 0th index contains the command which should not have wildcards.
*/
int indexOfWildcard(char** args){
  char* question = "?";
  char* star = "*";

  for(int i = 1; i < MAXARGS+1; i++){
    if(args[i]){
      if((strstr(args[i], question) != NULL) || (strstr(args[i], star) != NULL)){
        return i;
      }
    }
  }

  return 0;
}

// Call all of the functions to clean up allocated memory before exiting
void exitProtocol(char* prompt, char* commandline, char** args, char* owd, char*pwd, struct pathelement* pathlist){
  cleanup(prompt, commandline, args, owd, pwd);
  cleanupPathlist(pathlist);
}

/*
  Will return non-zero if the given path is a file, 0 otherwise.
  Used when a user enters an absolute or relative path to check whether it can
  be executed because it is a file or if it is a directory.
*/
int isFile(char* path){
  struct stat path_stat = {0};
  stat(path, &path_stat);
  return S_ISREG(path_stat.st_mode);
}


/*
  Creating a new argument array that will contain the expanded wildcard (files
  that matched the wildcard, if there were any) and all of the old arguments
 */
char** expandWildcard(char** args, int numArgs){
  glob_t globbuf = {0};                                                                                                                                                                                                                                                                                                                         // Find the wildcard index, will be 0 if there is no wildcard argument
int wildcardIndex = indexOfWildcard(args);

// Expanding the given wildcard using glob
glob(args[wildcardIndex], GLOB_DOOFFS, NULL, &globbuf);

int matchingFiles = (int)globbuf.gl_pathc;
int newArgsCount = matchingFiles + numArgs + 1;
char** newArgs = calloc(newArgsCount, sizeof(char*));;
int writeIndex = 0;

// Add all of the old arguments (except the potential wildcard) to the new arguments array
for(int i = 0; i < numArgs; i++){
  if(wildcardIndex > 0 && matchingFiles > 0){
    if(i != wildcardIndex){
      newArgs[writeIndex++] = args[i];
    }
  }

  else{
    newArgs[writeIndex++] = args[i];
  }
}

// Add all of the results from wildcard expansion with glob to the new arguments array
for(int i = 0; i < matchingFiles; i++){
  newArgs[writeIndex++] = globbuf.gl_pathv[i];
}

return newArgs;
}


int main(int argc, char **argv, char **envp){
/* Ignore ctrl-c, ctrl-z, and any commands to cause program termination */
signal(SIGINT, sig_handler);
signal(SIGTSTP, sig_handler);
signal(SIGTERM, sig_handler);

return sh(argc, argv, envp);
}
~                                                                                                                                                                      ~                                                                                                                                                                      ~
