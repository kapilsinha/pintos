# Caltech CS 124 Operating Systems
Building an operating system on Pintos (equivalent to Stanford's CS 140). 

## Project 1: Command Shell
* Parses a user command
* Handles forking and input/output redirection and piping
* Executes the command in a user program simulating the shell 

## Project 2: PC Booter
* Bootloader (written in IA32 assembly) loads a Pong game (written in C) into memory
* Transitions from real to protected mode and calls the entry to the Pong game
* Pong game utilizes ASCII for video output and timer & keyboard interrupts for state updates

## Project 3: Threads
* Implements lazy waiting for timer sleeps
* Implements priority scheduling for threads and priority donation for locks
* Implements a multilevel feedback queue scheduler (similar to 4.4BSD scheduler)

## Project 4: User Programs
* Implements argument passing for user programs
* Reads from user memory and handles errors and denies writes to executables
* Implements basic system calls (halt, exit, exec, wait, create, remove, open, filesize, read, write, seek, tell, close)

## Project 5: Virtual Memory
* Creates frame table and supplemental page table to map virtual pages to physical frames
* Implements loading and swapping of executables, stack pages, and mmap files
* Implements stack growth
* Implements page reclamation on process exit
* Implements eviction of pages via clock algorithm

## Project 6: File Systems
* Implements buffer cache to replace read/writes directly accessing file system
* Implements read/write locks for file system synchronization
* Replaces extent-based file system with indexing via direct, singly indirect, and doubly indirect blocks
* Implements subdirectories and relevant syscalls (chdir, mkdir, readdir, isdir, inumber)
