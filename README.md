# smallsh

## Table of contents
* [General Info](#general-info)
* [Technologies](#technologies)

## General Info
Smallsh is an implementation of a shell in C. A subset of features of well-known shells such as bash is included. The shell uses the PATH variable to look for non-built in commands and allows shell scripts to be executed. This program was created to further understand operating systems and currently handles the following features:

1. Provide a prompt for running commands
2. Handle blank lines and comments, which are lines beginning with the # character
3. Provide expansion for the variable $$
4. Execute 3 commands exit, cd, and status via code built into the shell
5. Execute other commands by creating new processes using a function from the exec family of functions
6. Support input and output redirection
7. Support running commands in foreground and background processes
8. Implement custom handlers for 2 signals, SIGINT and SIGTSTP

* Two implementations of smallsh were created. The "main_array.c" implementation stores the PIDs of non-completed background processes in an array. Each time before access to the command line is returned to the user, the status of these processes is checked using "waitpid(...NOHANG...)."
* The "main_signal.c" uses a signal handler to immediately wait() for child processes that terminate, in contrast to the first implementation of periodically checking a list of started background processes.

![alt text](img/fairview.gif)

## Technologies
This project is created with:
* C (gcc compiler, GNU99 standard)
* Unix process API
* Signal handling
* I/O redirection
	
