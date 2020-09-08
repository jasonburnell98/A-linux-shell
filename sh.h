
#include "get_path.h"
#include <glob.h>

typedef struct processThread{

  int created;
  int running;
  char *name;
  pthread_t thread;
  struct processThread *next;
  struct processThread *previous;

} processThread_t;

typedef struct watchElement {

  char *name;
  int size;
  struct utmpx *entry;
  struct watchElement *next;
  struct watchElement *previous;

} watch_t;

int sh( int argc, char **argv, char **envp);
int execute(int flag, char **commandpath, char* command, char **args, int argsct, struct pathelement *pathlist, char **envp, int redirect, int pipe[], char *pipeCommand);
int createPipe(char **commandpath, char* command, char **args, int *argsct, struct pathelement *pathlist, char **envp, char *pipeList[]);
void pipeInput(char *command, int pipe[]);
void pipeOutput(char *command, int pipe[]);
void mykill(char **args);
void cd(char **args, char *owd, char **pwd, char **oldpwd);
char *which(char *command, struct pathelement *pathlist);
void where(char *command, struct pathelement *pathlist);
void list ( char *dir );
void printenv(char **envp, char *arg);
void mysetenv (char **env, char **args, struct pathelement **pathlist, char **homedir);
void myprompt (char *prompt, char *arg);
void history (char **history, char* num);
void addHistory(char **commandhistory, char *commandline, int i);
int getArgs(char* commandline, char *arg, char **args);
int searchBuiltins(char*builtInList[], char* command);
void freePathlist(struct pathelement *pathlist);
glob_t wildcard(char *arg);
int checkDigits(char* string);
void sig_handler(int sig);
int addAlias(char **aliasArray, char **aliasCmdArray, char **args, int counter);
void watchuser(processThread_t *watchThread, char** args);
void *startWatchingUsers(void *arg);
int getUsers();
void addToWatchList(char* entry, watch_t **watchList);
void removeFromWatchList(char* entry, watch_t **watchList);
void freeWatchList(watch_t **watchList);
void watchMail(int *threadCount, char** args);
void *startWatchingMail();
int getMail(processThread_t *watchThread);
processThread_t *addToProcessList(char* entry, processThread_t **pList);
void removeFromProcessList(char* entry, processThread_t **pList);
void freeProcessList(processThread_t **pList);
int isRedirection(char **args, int argsct, char *redirections[]);
int checkRedirection (char *redirections[], char **args);
int redirection (int *argsct, char **redirect, char **rightOperand);
void restoreShell();
int fg (char *arg, int argsct);

#define PROMPTMAX 32
#define MAXARGS 10
#define MAXLINE 20
#define MAXTHREADS 10
