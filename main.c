#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>


int   allowBackground = 1;
int   backgroundCheck = 0;
int   argCount = 0;
int   processCount = 0;
int   processArray[500];
char* argArray[512];
char  currDir[100];
int   process;

struct sigaction SIGINTAction;
struct sigaction SIGTSTPAction;

void fork_child();
void fork_parent(pid_t child);
int  get_input(char* userInput);
void process_commands();
void exit_command();
void cd_command();
void status_command(int* errorCheck);
void process_alt_commands(int* errorCheck);
void handle_SIGTSTP();


// initializes sigaction structs for SIGINT and SIGTSTP, 
// then endlessly loops to read shell input lines
int main() {
	// holds all arguments as one continuous string
	char argsStr[2048];
	// initializes sigaction struct for SIGINT
	SIGINTAction.sa_handler = SIG_IGN;
	sigfillset(&SIGINTAction.sa_mask);
	sigaction(SIGINT, &SIGINTAction, NULL);
	// initializes sigaction struct for SIGTSTP
	SIGTSTPAction.sa_handler = handle_SIGTSTP;
	SIGTSTPAction.sa_flags = SA_RESTART;
	sigfillset(&SIGTSTPAction.sa_mask);
	sigaction(SIGTSTP, &SIGTSTPAction, NULL);
	// endless loop to get shell input lines (until user exits)
	while (1) {
		argCount = get_input(argsStr);
		argArray[argCount] = NULL;
		process_commands();
	}
	return 0;
}


// gets shell input lines from user
// returns number of arguments entered by user
int get_input(char* userInput) {
	int i;
	int localArgCount = 0;
	char tempStr[2048];
	printf(": ");
	// fflush is used throughout program to flush output buffers after printing
	fflush(stdout);
	// gets line of user input
	fgets(userInput, 2048, stdin);
	// newline is used as delimiter
	strtok(userInput, "\n");
	// retrieves first token
	char* token = strtok(userInput, " ");
	// retrieves and saves rest of tokens
	while (token != NULL) {
		argArray[localArgCount] = token;
		// expands instance of $$ into process ID of shell
		for (i = 1; i < strlen(argArray[localArgCount]); i++) {
			if (argArray[localArgCount][i] == '$' && argArray[localArgCount][i - 1] == '$') {
				argArray[localArgCount][i] = '\0';
				argArray[localArgCount][i - 1] = '\0';
				snprintf(tempStr, 2048, "%s%d", argArray[localArgCount], getpid());
				argArray[localArgCount] = tempStr;
			}
		}
		token = strtok(NULL, " ");
		localArgCount++;
	}
	return localArgCount;
}


// determines appropriate function needed to handle each argument retrieved from user input
// built-in commands (exit, cd, and status) are handled individually by shell
void process_commands() {
	int errorCheck = 0;
	// ignores comments indicated by '#' and blank lines
	// shell re-prompts user immediately following comment or blank line
	if (argArray[0][0] == '#' || argArray[0][0] == '\n') {
	}
	// individual functions are called for each built-in command
	else if (strcmp(argArray[0], "status") == 0) {
		status_command(&errorCheck);
	}
	else if (strcmp(argArray[0], "cd") == 0) {
		cd_command();
	}
	else if (strcmp(argArray[0], "exit") == 0) {
		exit_command();
	}
	// all other commands are handled by single function
	else {
		process_alt_commands(&errorCheck);
		// status is displayed if it has not been already
		if (WIFSIGNALED(process) && errorCheck == 0) {
			status_command(&errorCheck);
		}
	}
}


// built-in status command 
// prints extit status or terminating signal of last foreground process ran by shell
void status_command(int* errorCheck) {
	int errStatus = 0;
	int sigStatus = 0;
	int exitVal;
	// waits until most recently run process is completed and checks status
	waitpid(getpid(), &process, 0);
	// gets status of normally terminated child
	if (WIFEXITED(process))
		errStatus = WEXITSTATUS(process);
	// gets status of abnormally terminated child
	if (WIFSIGNALED(process))
		sigStatus = WTERMSIG(process);
	// exitVal is determined by status of terminated child
	exitVal = errStatus + sigStatus == 0 ? 0 : 1;
	if (sigStatus == 0)
		printf("exit value %d\n", exitVal);
	else {
		*errorCheck = 1;
		printf("terminated by signal %d\n", sigStatus);
	}
	fflush(stdout);
}


// built-in cd command
// changes cwd to directory specified in HOME environment variable if only cd is entered
// changes cwd to directory specified in arg if additional arg is provided other than cd
// prints new cwd or prints error if additional arg is invalid directory
void cd_command() {
	int track_err = 0;
	// if "cd" is only arg
	if (argCount == 1)
		track_err = chdir(getenv("HOME"));
	// if additional arg follows "cd"
	else
		track_err = chdir(argArray[1]);
	// if the cwd was successfully changed
	if (track_err == 0)
		printf("%s\n", getcwd(currDir, 100));
	// if there was an error changing the cwd
	else
		printf("chdir() failed\n");
	fflush(stdout);
}


// built-in exit command 
// exits shell and terminates any currently running processes
void exit_command() {
	// checks if any processes are currently running
	if (processCount == 0)
		// exits without terminating if no processes are running
		exit(0);
	// terminates currently running processes and then exits
	else {
		int i;
		for (i = 0; i < processCount; i++)
			kill(processArray[i], SIGTERM);
		exit(1);
	}
}


// handles execution of all non-built-in commands
// uses fork(), exec(), and waitpid() to achieve this
void process_alt_commands(int* errorCheck) {
	pid_t pid;
	backgroundCheck = 0;
	// presence of "&" following command causes process to be run in the background
	if (strcmp(argArray[argCount - 1], "&") == 0) {
		// checks if process is permitted to become background process
		// process will not be permitted if it is in foreground only mode
		if (allowBackground == 1)
			backgroundCheck = 1;
		argArray[argCount - 1] = NULL;
	}
	// creates new child process
	pid = fork();
	// saves pid of new child process
	processArray[processCount] = pid;
	// count of processes is incremented given that new process was created
	processCount++;
	switch (pid) {
		// error message is displayed if fork fails
	case -1:
		perror("fork() failed\n");
		exit(1);
		break;
		// triggered by child process
	case 0:
		// executes function for child process
		fork_child();
		break;
		// triggered by parent process
	default:
		// executes function for parent process
		fork_parent(pid);
	}
	// parent process waits for child process to terminate
	while ((pid = waitpid(-1, &process, WNOHANG)) > 0) {
		printf("background pid %d is done: ", pid);
		fflush(stdout);
		status_command(errorCheck);
	}
}


// registers custom signal handler for SIGTSTP triggered by user input of CTRL-Z
// flips between foreground-only mode and normal mode upon user input of CTRL-Z
// first input of CTRL-Z enters foreground-only mode (allowBackground initialized to 1)
void handle_SIGTSTP() {
	char* message;
	int messageSize = -1;
	char* shellPrompt = ": ";
	switch (allowBackground) {
	case 0:
		message = "\nExiting foreground-only mode\n";
		messageSize = 30;
		// set to 1 since user will enter foreground-only mode after next CTRL-Z input
		allowBackground = 1;
		break;
	case 1:
		message = "\nEntering foreground-only mode (& is now ignored)\n";
		messageSize = 50;
		// set to 0 since user will exit foreground-only mode after next CTRL-Z input
		allowBackground = 0;
		break;
	default:
		message = "\nError: allowBackground is not 0 or 1\n";
		messageSize = 38;
		allowBackground = 1;
	}
	// write is reentrant alternative to printf, required for custom signal handlers
	write(STDOUT_FILENO, message, messageSize);
	write(STDOUT_FILENO, shellPrompt, 2);
}


// handles parent process
// waits for foreground/background child process to finish (if necessary)
void fork_parent(pid_t child) {
	// if child process if background process
	if (backgroundCheck == 1) {
		waitpid(child, &process, WNOHANG);
		printf("background pid is %d\n", child);
		fflush(stdout);
	}
	// if child process if foreground process
	else {
		waitpid(child, &process, 0);
	}
}


// executes non-built in commands for child process using execvp
// checks for file input/ouput commands and handles appropriately
void fork_child() {
	int i; 
	int inputFileCheck = 0; 
	int outputFileCheck = 0;
	char inputFile[2048], outputFile[2048];
	// retrieve and individually process command arguments
	for (i = 0; argArray[i] != NULL; i++) {
		// checks for input file command indicated by "<"
		if (strcmp(argArray[i], "<") == 0) {
			// used to track if there is an input file
			inputFileCheck = 1;
			argArray[i] = NULL;
			// saves name of input file
			strcpy(inputFile, argArray[i + 1]);
			i++;
		}
		// checks for output file command indicated by ">"
		else if (strcmp(argArray[i], ">") == 0) {
			// used to track if there is an output file
			outputFileCheck = 1;
			argArray[i] = NULL;
			// saves name of output file
			strcpy(outputFile, argArray[i + 1]);
			i++;
		}
	}
	// handles input file command
	if (inputFileCheck) {
		int inputFD = 0;
		// prints error message if file cannot be opened
		if ((inputFD = open(inputFile, O_RDONLY)) < 0) {
			fprintf(stderr, "cannot open %s for input\n", inputFile);
			fflush(stdout);
			exit(1);
		}
		// creates copy of file descriptor
		dup2(inputFD, 0);
		close(inputFD);
	}
	// handles output file command
	if (outputFileCheck) {
		int outputFD = 0;
		// prints error message if file cannot be opened
		if ((outputFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
			fprintf(stderr, "cannot open %s for output\n", outputFile);
			fflush(stdout);
			exit(1);
		}
		// creates copy of file descriptor
		dup2(outputFD, 1);
		close(outputFD);
	}
	// resets SIGINT handler, CTRL-C terminates foreground processes
	if (!backgroundCheck)
		SIGINTAction.sa_handler = SIG_DFL;
	sigaction(SIGINT, &SIGINTAction, NULL);
	// execvp is used to run the non-built-in command
	if (execvp(argArray[0], argArray) == -1) {
		perror(argArray[0]);
		exit(1);
	}
}