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
#define PROCESS_LIMIT 200

/* struct for user input */
struct command
{
    char *args[ARGS_LIMIT];             // Array to hold command arguments
    int argSize;                        // Holds the next empty index of args array
    int argExp;                         // Holds the index of the argument to free from memory
    _Bool background;                   // Flag for a background process command
    int exitStatus;                     // Exit status of the last foreground process
    _Bool signalTerm;                   // Flag for a process terminated by a signal
    int shellPid;                       // smallsh PID
    char *inputFile;                    // String of the input location for redirection
    _Bool inputRe;                      // Flag for input redirection
    char *outputFile;                   // String of the output location for redirection
    _Bool outputRe;                     // Flag for output redirection
    _Bool backgroundOff;                // Flag to enable or disable background commands via SIGTSTP
    int backgroundPids[PROCESS_LIMIT];  // Array to hold background process PIDs
    int bgPidSize;                      // Holds the next empty index of backroundPids array
};

/* Function prototypes */
int parseCommand(char *userInput);
int executeCommand(void);
char *varExp(char *token);
void handleSIGTSTP(int signo);

/* Global variables */
struct command inputs;
struct sigaction ignoreAction = {{0}}, defaultAction = {{0}}, actionSIGTSTP = {{0}};

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
    // Set array of pointers, args, to NULL for all elements for both arrays
    memset(inputs.args, 0, ARGS_LIMIT * sizeof(inputs.args[0]));
    memset(inputs.backgroundPids, 0, PROCESS_LIMIT * sizeof(inputs.backgroundPids[0]));

    // Initialize args size and background PIDs array size
    inputs.argSize = 0, inputs.bgPidSize = 0;

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

    /* Main event loop */
    while (1) {
        // Check if any non-completed background processes are finished
        int i, childPid, childExitStatus;
        for (i = 0; i < PROCESS_LIMIT; i++) {
            // If there is a non-completed background process
            if (inputs.backgroundPids[i] != 0) {
                childPid = inputs.backgroundPids[i];
                childPid = waitpid(childPid, &childExitStatus, WNOHANG);
                if (childPid > 0) {
                    // Background process has been completed, check and set exit status
                    int exitStatus;
                    if (WIFEXITED(childExitStatus)) {
                        // Child background process terminated normally, print result to the terminal
                        exitStatus = WEXITSTATUS(childExitStatus);
                        printf("background pid %d is done: exit value %d\n", childPid, exitStatus);
                        fflush(stdout);
                    }
                    else if (WIFSIGNALED(childExitStatus)) {
                        // Child background process terminated abnormally, print result to the terminal
                        exitStatus = WTERMSIG(childExitStatus);
                        printf("background pid %d is done: terminated by signal %d\n", childPid, exitStatus);
                        fflush(stdout);
                    }
                    // Clear the background PID from the array since the process was completed
                    inputs.backgroundPids[i] = 0;
                }
            }
        }

        // Present access of the command line to the user
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
        i = numChars - 2;
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

    /* Fork and exec user inputted commands */
    pid_t childPid = fork();
    switch (childPid) {
        /* Fork error */
        case -1: {
            perror("fork() error!\n");
            exit(1);
            break;
        }
        /* Child process */
        case 0: {
            // Initialize variables as file descriptors for redirection if needed
            int sourceFD, targetFD, result;

            // Check for input redirection
            if (inputs.inputFile != NULL || (inputs.inputFile == NULL && inputs.background)) {
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
                    exit(1);
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
            // If foreground process is being run, set SIGINT back to default
            sigaction(SIGINT, &defaultAction, NULL);
            // Run the program using execvp in the child process
            execvp(inputs.args[0], inputs.args);
            // exec only returns if there is an error
            perror(inputs.args[0]);
            exit(1);
            break;
        }
        /* Parent process */
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
                // Reset SIGINT signal handler back to ignore
                sigaction(SIGINT, &ignoreAction, NULL);
            }

            // Print child background pid if background process
            if (inputs.background) {
                // Print child background pid
                printf("background pid is: %d\n", childPid);
                fflush(stdout);
                // Store the child background pid in an array
                inputs.backgroundPids[inputs.bgPidSize] = childPid;
                inputs.bgPidSize++;
                // Reset bgPidSize index value if over the size of the array
                if (inputs.bgPidSize == 200) {
                    inputs.bgPidSize = 0;
                }
            }
        }
    }
    return 0;
}

/*
* Perform variable expansion of an instance of "$$"; replace "$$" with the shell's PID
*/
char *varExp(char *token) {
    // Convert shell's PID to string
    char shellPidStr[MAX_PID_LENGTH];
    sprintf(shellPidStr, "%d", inputs.shellPid);

    int dollarNum = 0;  // Initialize number of "$$" to replace
    int isOdd = 0;      // Initialize odd or even number of "$" in string
    
    // Count the number of "$" in the string
    int i;
    for (i = 0; i < strlen(token); i++) {
        if (token[i] == '$') {
            dollarNum++;
        }
    }
    // Length of string without "$" instances
    int lenStr = strlen(token) - dollarNum;
    if (dollarNum % 2 == 0) {
        // If the number of "$" to replace is even
        dollarNum = dollarNum / VAR_LENGTH;
    }
    else {
        // If the number of "$" to replace is odd
        dollarNum = dollarNum / VAR_LENGTH;
        isOdd = 1;
    }

    // Malloc memory for the new string
    char *newToken = malloc(lenStr + strlen(shellPidStr) * dollarNum + isOdd);
    memset(newToken, '\0', strlen(newToken) * sizeof(char));
    char *newTokenPtr = newToken;

    // Iterate over the original string and replace "$$" instances with the shell PID
    i = 0;
    char src[2];
    while (i < strlen(token)) {
        // Replace "$$" with shell PID
        if (token[i] == '$' && token[i + 1] == '$') {
            newTokenPtr = strcat(newTokenPtr, shellPidStr);
            i = i + 2;
        }
        // Else, copy over the single char
        else {
            src[0] = token[i];
            src[1] = '\0';
            newTokenPtr = strcat(newTokenPtr, src);
            i++;
        }
    }
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
