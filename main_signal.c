#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/* Define macros */
#define ARGS_LIMIT 512
#define VAR_LENGTH 2
#define MAX_PID_LENGTH 7
#define MAX_EXIT_STATUS 4

/* struct for user input */
struct command
{
    char *args[ARGS_LIMIT];  // Array to hold command arguments
    int argSize;             // Holds the next empty index of args array
    int argExp;              // Holds the index of the argument to free from memory
    _Bool background;        // Flag for a background process command
    int exitStatus;          // Exit status of the last foreground process
    _Bool signalTerm;        // Flag for a process terminated by a signal
    int shellPid;            // smallsh PID
    char *inputFile;         // String of the input location for redirection
    _Bool inputRe;           // Flag for input redirection
    char *outputFile;        // String of the output location for redirection
    _Bool outputRe;          // Flag for output redirection
    _Bool backgroundOff;     // Flag to enable or disable background commands via SIGTSTP
};

/* Function prototypes */
int parseCommand(char *userInput);
int executeCommand(void);
char *varExp(char *token);
void handleSIGTSTP(int signo);
void handleSIGCHLD(int signo);

/* Global variables */
struct command inputs;
struct sigaction ignoreAction = {{0}}, defaultAction = {{0}}, actionSIGTSTP = {{0}}, actionSIGCHLD = {{0}};

/*
* Citation for the following signal handler initialization code segment:
* Date: 02/01/2022
* Adapted from: Exploration: Signal Handling API
* Source URLS: https://canvas.oregonstate.edu/courses/1884946/pages/exploration-signal-handling-api?module_item_id=21835981
* Description: Initialize and set signal handlers;
*              Main function to display a command line interface and handle commands made by the user
*/
int main(void) {
    /* Initialize command struct */
    // Set array of pointers, args, to NULL for all elements
    memset(inputs.args, 0, ARGS_LIMIT * sizeof(inputs.args[0]));

    // Initialize args size
    inputs.argSize = 0;

    // Set flags to 0, as no processes have been run
    inputs.argExp = 0;
    inputs.background = 0;
    inputs.exitStatus = 0;
    inputs.signalTerm = 0;
    inputs.inputRe = 0;
    inputs.outputRe = 0;
    inputs.backgroundOff = 0;

    // Set input and output file pointers to NULL
    inputs.inputFile = NULL;
    inputs.outputFile = NULL;

    // Get and store shell PID for variable expansion
    inputs.shellPid = getpid();

    /* Initialize signal handlers */
    // Set ignoreAction struct as SIG_IGN for its signal handler
    ignoreAction.sa_handler = SIG_IGN;

    // Set defaultAction struct as SIG_DFL for its signal handler
    defaultAction.sa_handler = SIG_DFL;

    /* SIGINT */
    // Install the ignoreAction as the handler for SIGINT
    sigaction(SIGINT, &ignoreAction, NULL);

    /* SIGTSTP */
    // Register actionSIGTSTP as the signal handler
    actionSIGTSTP.sa_handler = handleSIGTSTP;
    // Block all catchable signals while handleSIGTSTP is running
    sigfillset(&actionSIGTSTP.sa_mask);
    // No flags set
    actionSIGTSTP.sa_flags = 0;
    // Install the actionSIGTSTP signal handler
    sigaction(SIGTSTP, &actionSIGTSTP, NULL);

    /* SIGCHLD */
    // Register actionSIGCHLD as the signal handler
    actionSIGCHLD.sa_handler = handleSIGCHLD;
    // Block all catchable signals while handleSIGCHLD is running
    sigfillset(&actionSIGCHLD.sa_mask);
    // No flags set
    actionSIGCHLD.sa_flags = 0;
    // Install the actionSIGCHLD signal handler
    sigaction(SIGCHLD, &actionSIGCHLD, NULL);

    /* Main event loop */
    while (1) {
        printf(": ");
        fflush(stdout);
        // For use with getline()
        char *userInput = NULL;
        size_t bufferSize = 0;
        // Take in a file name with spaces as necessary
        int numChars = getline(&userInput, &bufferSize, stdin);
        // If there is an error, handle the error
        if (numChars == -1) {
            clearerr(stdin);
            free(userInput);
            continue;
        }
        // Else, set the last character in the string as taken in by getline() from '\n' to '\0', for comparison in other functions
        userInput[numChars - 1] = '\0';
        // Remove any extra whitespace at the end of the input
        int i = numChars - 2;
        while (i >= 0 && userInput[i] == ' ') {
            userInput[i] = '\0';
            i--;
        }
        
        /* Initial user input parsing */
        // Handle blank lines (without any commands) or comments:
        if (userInput[0] == '\0') {
            // Pass this input and go to free userInput
        }
        else if (userInput[0] == '#') {
            // Print newline for formatting and go to free userInput
            printf("\n");
            fflush(stdout);
        }
        /* Built-in functions */
        // If only "exit" is entered with no arguments; & is ignored for built-in commands
        else if (strcmp(userInput, "exit") == 0 || strcmp(userInput, "exit &") == 0) {
            // Free the memory from userInput
            free(userInput);
            // Break out of the loop and return 0
            break;   
        }
        // If only "cd" is entered with no arguments, change directory to HOME env variable; & is ignored for built-in commands
        else if (strcmp(userInput, "cd") == 0 || strcmp(userInput, "cd &") == 0) {
            // Get HOME directory path
            char *homePath = getenv("HOME");
            // Set current working directory to HOME
            chdir(homePath);
        }
        // If only "status" is entered with no arguments; & is ignored for built-in commands
        else if (strcmp(userInput, "status") == 0 || strcmp(userInput, "status &") == 0) {
            if (!inputs.signalTerm) {
                // Return the exit status
                printf("exit value %d\n", inputs.exitStatus);
                fflush(stdout);
            }
            else {
                // Return the last signal status by a foreground process
                printf("terminated by signal %d\n", inputs.exitStatus);
                fflush(stdout);
            }
        }
        /* Parse the user inputted command */
        else {
            parseCommand(userInput);
        }

        // Reset args size and background flag
        inputs.argSize = 0;
        inputs.background = 0;
        // Reset input & output strings
        if (inputs.inputFile != NULL) {
            inputs.inputFile = NULL;
        }
        if (inputs.outputFile != NULL) {
            inputs.outputFile = NULL;
        }
        // Free memory if variable expansion was done
        if (inputs.argExp) {
            free(inputs.args[inputs.argExp]);
            inputs.argExp = 0;
        }
        // Reset array of pointers, args, to NULL for all elements
        memset(inputs.args, 0, ARGS_LIMIT * sizeof(inputs.args[0]));
        // Free the memory from userInput after each loop
        free(userInput);
    }

    /* Wait to end child processes if any, before returning */
    int childStatus, childPid;
    do {
        // If there are no child processes, wait returns -1 immediately 
        childPid = wait(&childStatus);
    } while (childPid != -1);

    // return 0 by main() calls exit(), which calls _exit(), which closes all files and performs clean-up
    return 0;
}

/*
* Parse user inputted command
*/
int parseCommand(char *userInput) {
    // Boolean values to process cd argument and to distinguish background commands
    _Bool cdArg = 0;
    _Bool bgArg = 0;
    
    // For use with strtok_r
    char *saveptr;

    // Check if the token includes "&" at the end for a background process
    if (userInput[strlen(userInput) - 1] == '&') {
        bgArg = 1;
    }

    // The first token is the first command arg
    char *token = strtok_r(userInput, " ", &saveptr);
    // Check if token is "cd"
    if (strcmp(token, "cd") == 0) {
        cdArg = 1;
    }
    // Else, store the first argument in the array
    else {
        inputs.args[inputs.argSize] = token;
        inputs.argSize++;
    }

    while (token != NULL) {
        // The next tokens are the second+ args; 
        token = strtok_r(NULL, " ", &saveptr);
        
        // If token is NULL and the argument to be executed is "cd"
        if (token == NULL && cdArg) {
            // Execute chdir with the argument
            chdir(inputs.args[inputs.argSize - 1]);
        }
        // If token is NULL, execute the command with the collected inputs
        else if (token == NULL) {
            /* Execute command */
            executeCommand();
        }
        // Check if the token has an instance of "$$", for variable expansion 
        else if (strstr(token, "$$")) {  // strstr() returns a pointer at the location where the substring "$$" was found
            // token becomes the new variable after expansion
            token = varExp(token);
            // Store index that the new variable is stored in to free memory later
            inputs.argExp = inputs.argSize;
            // Store token in array
            inputs.args[inputs.argSize] = token;
            inputs.argSize++;
        }
        // Check if the token is "<" or ">" for input & output redirection
        else if (strcmp(token, "<") == 0) {
            inputs.inputRe = 1;
        }
        else if (strcmp(token, ">") == 0) {
            inputs.outputRe = 1;
        }
        // Check for the token is "&" and handle accordingly
        else if (strcmp(token, "&") == 0 && !inputs.backgroundOff && bgArg) {
            inputs.background = 1;
        }
        else if (strcmp(token, "&") == 0 && inputs.backgroundOff && bgArg) {
            // Don't process background commands if backgroundOff flag is True
            continue;
        }
        // Else, the token is not a shell specific operator (not <, >, or &)
        else {
            // Check input redirection flag
            if (inputs.inputRe) {
                inputs.inputFile = token;
                inputs.inputRe = 0;
            }
            // Check output redirection flag
            else if (inputs.outputRe) {
                inputs.outputFile = token;
                inputs.outputRe = 0;
            }
            // Else, store the token as a command in the array
            else {
                inputs.args[inputs.argSize] = token;
                inputs.argSize++;
            }
        }
    }

    return 0;
}

/*
* Citation for the following function:
* Date: 01/28/2022
* Adapted from: Exploration: Process API - Executing a New Program; Exploration: Processes and I/O
* Source URLS: https://canvas.oregonstate.edu/courses/1884946/pages/exploration-process-api-executing-a-new-program?module_item_id=21835974
*              https://canvas.oregonstate.edu/courses/1884946/pages/exploration-processes-and-i-slash-o?module_item_id=21835982
* Description: Execute a new program by forking a child process and calling an exec function to run the new program
*/
int executeCommand(void) {
    // Initialize variable to hold child exit status from forked child process
    int childExitStatus;
    // Initialize variables to hold stdin & stdout and file descriptors for redirection if needed
    int curStdin = 0, curStdout = 0;
    int sourceFD, targetFD, result;

    // Check for input redirection
    if (inputs.inputFile != NULL || (inputs.inputFile == NULL && inputs.background)) {
        // Save current stdin to restore later
        curStdin = dup(0);
        // Open source file
        if (inputs.background) {
            // Background command was made and stdin was not redirected; redirect to /dev/null
            sourceFD = open("/dev/null", O_RDONLY);
        }
        else {
            // Foreground command was made and stdin was redirected
            sourceFD = open(inputs.inputFile, O_RDONLY);
        }
        if (sourceFD == -1) {
            printf("cannot open %s for input\n", inputs.inputFile);
            fflush(stdout);
            // Child terminated normally, set signal terminated flag to False
            inputs.signalTerm = 0;
            // Store the status value
            inputs.exitStatus = 1;
            return 0;
        }
        // Redirect stdin to source file
        result = dup2(sourceFD, 0);
        if (result == -1) {
            perror("source dup2() error!");
            exit(1);
        }
    }

    // Check for output redirection
    if (inputs.outputFile != NULL || (inputs.outputFile == NULL && inputs.background)) {
        // Save current stdout to restore later
        curStdout = dup(1);
        // Open target file
        if (inputs.background) {
            // Background command was made and stdout was not redirected; redirect to /dev/null
            targetFD = open("/dev/null", O_WRONLY);
        }
        else {
            // Foreground command was made and stdout was redirected
            targetFD = open(inputs.outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        if (targetFD == -1) {
            perror("target open() error!");
            exit(1);
        }
        // Redirect stdout to target file
        result = dup2(targetFD, 1);
        if (result == -1) {
            perror("target dup2() error!");
            exit(1);
        }
    }

    // Check for foreground process
    if (!inputs.background) {
        // Set SIGCHLD signal handler to default action
        sigaction(SIGCHLD, &defaultAction, NULL);
    }

    /* Fork and exec user inputted commands */
    pid_t childPid = fork();
    switch (childPid) {
        case -1: {
            perror("fork() error!\n");
            exit(1);
            break;
        }
        case 0: {
            // If foreground process is being run, set SIGINT back to default
            sigaction(SIGINT, &defaultAction, NULL);
            // Run the program using execvp in the child process
            execvp(inputs.args[0], inputs.args);
            // exec only returns if there is an error
            perror(inputs.args[0]);
            exit(1);
            break;
        }
        default: {
            /* Background command */
            if (inputs.background) {
                // Run waitpid with WNOHANG option; if the child hasn't terminated, waitpid returns immediately with value 0
                waitpid(childPid, &childExitStatus, WNOHANG);
            }
            /* Foreground command */
            else {
                // Wait for the child process and block before continuing
                childPid = waitpid(childPid, &childExitStatus, 0);
                // Check and set exit status
                if (WIFEXITED(childExitStatus)) {
                    // If child terminated normally, set signal terminated flag to False
                    inputs.signalTerm = 0;
                    // Store the status value
                    inputs.exitStatus = WEXITSTATUS(childExitStatus);
                }
                else if (WIFSIGNALED(childExitStatus)) {
                    // If child terminated abnormally, set signal terminated flag to True
                    inputs.signalTerm = 1;
                    // Store the status value
                    inputs.exitStatus = WTERMSIG(childExitStatus);
                    // Immediately print out the number of the signal that killed the foreground child process
                    printf("terminated by signal %d\n", inputs.exitStatus);
                    fflush(stdout);
                }
                // If childPid returns -1, wait for any process before continuing
                if (childPid == -1) {
                    childPid = waitpid(-1, &childExitStatus, 0);
                }
                // Reset SIGINT signal handler back to ignore
                sigaction(SIGINT, &ignoreAction, NULL);
                // Reset SIGCHLD signal handler back to function
                sigaction(SIGCHLD, &actionSIGCHLD, NULL);
                // Raise SIGCHLD to check for completed background processes
                raise(SIGCHLD);
            }

            // Restore stdin if input redirection was done
            if (curStdin) {
                // Restore stdin back to keyboard
                result = dup2(curStdin, 0);
                if (result == -1) {
                    perror("source dup2() restore error!");
                    exit(2);
                }
                // Close file descriptors
                close(sourceFD);
                close(curStdin);
            }
            // Restore stdout if output redirection was done
            if (curStdout) {
                // Restore stdout back to terminal
                result = dup2(curStdout, 1);
                if (result == -1) {
                    perror("target dup2() restore error!");
                    exit(2);
                }
                // Print a newline for formatting
                printf("\n");
                fflush(stdout);
                // Close file descriptors
                close(targetFD);
                close(curStdout);
            }

            // stdout was restored, print child background pid if background process
            if (inputs.background) {
                // Print child background pid
                printf("background pid is: %d\n", childPid);
                fflush(stdout);
            }
        }
    }

    return 0;
}

/*
* Citation for the following function:
* Date: 01/30/2022
* Adapted from: stackoverflow - "What function is to replace a substring from a string in C?"
* Source URL: https://stackoverflow.com/questions/779875/what-function-is-to-replace-a-substring-from-a-string-in-c
* Description: Perform variable expansion of an instance of "$$"; replace "$$" with the shell's PID
*/
char *varExp(char *token) {
    // Convert shell's PID to string
    char shellPidStr[MAX_PID_LENGTH];
    sprintf(shellPidStr, "%d", inputs.shellPid); 
    
    char *newToken, *curPtr, *temp;  // Initialize pointer to return the new string, pointer to insert the PID, and temp pointer, respectively
    int lenVar = VAR_LENGTH;  // Initialize length of string "$$"
    int lenPid = strlen(shellPidStr);  // Initialize length of string shell PID
    int lenDist;  // Initialize distance between "$$"
    int replaces = 0;  // Initialize number of "$$" to replace
    
    // Count the number of "$$" to replace
    int i;
    for (i = 0; i < strlen(token); i++) {
        if (token[i] == '$') {
            replaces++;
        }
    }
    replaces = replaces / VAR_LENGTH;

    // Malloc memory to use with string functions
    newToken = malloc(strlen(token) + (lenPid - lenVar) * replaces + 1);
    temp = newToken;

    // curPtr points to next instance of "$$" in token, token points to next substring after "$$" replaced, temp points to end of newToken
    while (replaces--) {
        curPtr = strstr(token, "$$");
        lenDist = curPtr - token;
        temp = strncpy(temp, token, lenDist) + lenDist;
        temp = strcpy(temp, shellPidStr) + lenPid;
        token += lenDist + lenVar;
    }
    strcpy(temp, token);

    // newToken holds new variable after expansion
    return newToken;
}

/*
* Signal handler for SIGTSTP
*/
void handleSIGTSTP(int signo) {
    // If background processes are currently enabled, set background processes off
    if (!inputs.backgroundOff) {
        inputs.backgroundOff = 1;
        char *message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
    }
    // Else, background processes are currently disabled, set background processes on
    else {
        inputs.backgroundOff = 0;
        char *message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
    }
}

/*
* Signal handler for SIGCHLD
*/
void handleSIGCHLD(int signo) {
    // A foreground or background child process of this parent process has terminated, stopped, or continued
    int childStatus, childPid, exitStatus;
    // Non-blocking wait for the childPid using pid = -1 to wait for any child process
    childPid = waitpid(-1, &childStatus, WNOHANG);
    // If waitpid did not return -1 or 0, a child process was completed
    if (childPid > 0) {
        char *message1 = "background pid ";
        // Write out message1 using non-reentrant function write()
        write(STDOUT_FILENO, message1, 15);

        // Write out childPid
        int i = 1;
        while (childPid / (i * 10) != 0) i *= 10;
        for (; 0 < i; i /= 10) { 
            char c = (char) (childPid / i) + '0';
            write(STDOUT_FILENO, &c, 1);
            childPid = childPid % i;
        }
        
        // Check and set exit status
        if (WIFEXITED(childStatus)) {
            // If child terminated normally, print out the pid and the exit status
            exitStatus = WEXITSTATUS(childStatus);
            char *message2 = " is done: exit value ";
            // Write out message2
            write(STDOUT_FILENO, message2, 21);
        }
        else if (WIFSIGNALED(childStatus)) {
            // If child terminated abnormally, print out the pid and the exit status
            exitStatus = WTERMSIG(childStatus);
            char *message2 = " is done: terminated by signal ";
            // Write out message2
            write(STDOUT_FILENO, message2, 31);
        }

        // Write out exit value
        i = 1;
        while (exitStatus / (i * 10) != 0) i *= 10;
        for (; 0 < i; i /= 10) { 
            char c = (char) (exitStatus / i) + '0';
            write(STDOUT_FILENO, &c, 1);
            exitStatus = exitStatus % i;
        }

        char *message3 = "\n";
        // Write out message3
        write(STDOUT_FILENO, message3, 1);
    }
}
