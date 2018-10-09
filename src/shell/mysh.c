#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

// We are told to assume the command is less than 1 KiB
#define MAX_COMMAND_LENGTH 1024
// maximum path length for Linux is 4096 chars
#define MAX_PATH_SIZE 4096
// maximum filename length for Linux is 256 chars
#define MAX_FILENAME_SIZE 256

typedef struct command {
    int input;
    int output;
    int error;
    int num_args;
    char **args;
} Command;

typedef struct commands {
    int num_commands;
    Command *commands;
} Commands;

typedef struct token_pair {
    int num_tokens;
    char **tokens;
} TokenPair;

TokenPair tokenize(char *command) {
    int start_index = 0;
    int end_index = 0;
    int in_quotes = 0; // 1 if current character is within quotes, else 0
    // Max number of tokens is MAX_COMMAND_LENGTH
    char **tokens = (char **) malloc(MAX_COMMAND_LENGTH * sizeof(char *));
    int num_tokens = 0;
    for (int i = 0; i < strlen(command); i++) {
        // If character is in quotes, ignore any redirections or pipes
        if (in_quotes && command[i] != '\"') {
            end_index += 1;
            continue;
        }
        // If start_index corresponds to whitespace, increment start_index
        // (which then equals i)
        else if (command[start_index] == ' ' || command[start_index] == '\t') {
            start_index += 1;
            end_index += 1;
        }
        // Handle all possible ends of a token
        else if (command[i] == ' ' || command[i] == '\t' || command[i] == '\"'
                 || command[i] == '>' || command[i] == '<' || command[i] == '|'
                 || i == strlen(command) - 1) {
            if (command[i] == '\"') {
                if (in_quotes) {
                    in_quotes = 0;
                    end_index = i + 1;
                }
                else {
                    in_quotes = 1;
                }
            }

            if (end_index > start_index) { // Tokenize [start_index, end_index)
                tokens[num_tokens] = (char *)
                    malloc ((end_index - start_index + 1) * sizeof(char));
                strncpy(tokens[num_tokens], command + start_index,
                    end_index - start_index);
                // Need to manually null terminate these strings
                tokens[num_tokens][end_index - start_index] = '\0';
                num_tokens += 1;
            }

            // Also tokenize the redirects and pipes
            if (command[i] == '>' || command[i] == '<' || command[i] == '|') {
                tokens[num_tokens] = (char *) malloc (2 * sizeof(char));
                strncpy(tokens[num_tokens], command + i, 1);
                // Need to manually null terminate these strings
                tokens[num_tokens][1] = '\0';
                num_tokens += 1;
            }
            if (command[i] == '\"') {
                start_index = end_index;
            }
            else {
                start_index = end_index + 1;
                end_index = end_index + 1;
            }
        }
        else {
            end_index += 1;
        }
    }
    TokenPair pair;
    pair.num_tokens = num_tokens;
    pair.tokens = tokens;
    return pair;
}

Commands generate_commands(TokenPair pair) {
    Commands commands;
    commands.num_commands = 1;
    for (int i = 0; i < pair.num_tokens; i++) {
        if (strcmp(pair.tokens[i], "|") == 0) {
            commands.num_commands += 1;
        }
    }
    commands.commands = (Command *)
                        malloc(commands.num_commands * sizeof(Command));

    for (int i = 0; i < commands.num_commands; i++) {
        commands.commands[i].input = STDIN_FILENO;
        commands.commands[i].output = STDOUT_FILENO;
        commands.commands[i].error = STDERR_FILENO;
        commands.commands[i].num_args = 0;
        commands.commands[i].args = (char **) NULL;
    }
    int j = 0;
    int io_redirection = 0;
    for (int i = 0; i < pair.num_tokens; i++) {
        if (strcmp(pair.tokens[i], "|") == 0) {
            j += 1;
            io_redirection = 0;
        }
        else if (strcmp(pair.tokens[i], "<") == 0
                 || strcmp(pair.tokens[i], ">") == 0) {
            io_redirection = 1;
        }
        else if (!io_redirection) {
            commands.commands[j].num_args += 1;
        }
    }
    for (int i = 0; i < commands.num_commands; i++) {
        commands.commands[i].args = (char **)
                                    malloc((commands.commands[i].num_args + 1)
                                           * sizeof(char *));
    }
    int k = 0;
    int l = 0;
    // 1 if immediately after an input redirection symbol, else 0
    int input_redirection = 0;
    // 1 if immediately after an output redirection symbol, else 0
    int output_redirection = 0;
    for (int i = 0; i < pair.num_tokens; i++) {
        if (strcmp(pair.tokens[i], "|") == 0) {
            commands.commands[k].args[l] = (char *) NULL;
            k += 1;
            l = 0;
            input_redirection = 0;
            output_redirection = 0;
        }
        else if (strcmp(pair.tokens[i], "<") == 0) {
            input_redirection = 1;
            output_redirection = 0;
        }
        else if (strcmp(pair.tokens[i], ">") == 0) {
            input_redirection = 0;
            output_redirection = 1;
        }
        else if (input_redirection || output_redirection) {
            char *filename;
            char *src;
            size_t len;
            if (strlen(pair.tokens[i]) >= 2 && pair.tokens[i][0] == '\"'
                && pair.tokens[i][strlen(pair.tokens[i]) - 1] == '\"') {
                src = pair.tokens[i] + 1;
                len = strlen(pair.tokens[i]) - 2;
            }
            else {
                src = pair.tokens[i];
                len = strlen(pair.tokens[i]);
            }
            filename = (char *) malloc((len + 1) * sizeof(char));
            strncpy(filename, src, len);
            filename[len] = '\0';

            // Just set the input or output to the file descriptor and change
            // stdin or stdout later when executing the process
            int in_fd, out_fd;
            if (input_redirection) {
                in_fd = open(filename, O_RDONLY, S_IRUSR | S_IWUSR);
                commands.commands[k].input = in_fd;
            }

            if (output_redirection) {
                out_fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
                commands.commands[k].output = out_fd;
            }

            // TODO: Take care of pipes
        }
        else {
            char *src;
            size_t len;
            if (strlen(pair.tokens[i]) >= 2 && pair.tokens[i][0] == '\"'
                && pair.tokens[i][strlen(pair.tokens[i]) - 1] == '\"') {
                src = pair.tokens[i] + 1;
                len = strlen(pair.tokens[i]) - 2;
            }
            else {
                src = pair.tokens[i];
                len = strlen(pair.tokens[i]);
            }
            commands.commands[k].args[l] = (char *)
                                            malloc((len + 1) * sizeof(char));
            strncpy(commands.commands[k].args[l], src, len);
            commands.commands[k].args[l][len] = '\0';
            l += 1;
        }
    }
    commands.commands[k].args[l] = (char *) NULL;

    return commands;
}

int execute_command(Command command) {
    dup2(command.input, STDIN_FILENO);
    dup2(command.output, STDOUT_FILENO);
    execvp(command.args[0], command.args);
    // int err = errno;
    perror("Error in executing command");
    return 0;
}

void shell() {
    // Print prompt
    char cwd[MAX_PATH_SIZE];
    getcwd(cwd, sizeof(cwd));
    printf("%s:%s> ", getpwuid(getuid())->pw_name, cwd);
    char command [MAX_COMMAND_LENGTH];
    fgets(command, MAX_COMMAND_LENGTH, stdin);
    TokenPair pair = tokenize(command);
    Commands commands = generate_commands(pair);
    int num_commands = commands.num_commands;
    Command *commands_arr = commands.commands;

    printf("Number of commands: %d\n", num_commands);
    printf("========================================"
           "========================================\n");
    for (int i = 0; i < num_commands; i++) {
        printf("Input: %d\n\n", commands_arr[i].input);
        printf("Output: %d\n\n", commands_arr[i].output);
        printf("Error: %d\n\n", commands_arr[i].error);
        printf("Number of arguments: %d\n\n", commands_arr[i].num_args);
        printf("Arguments:\n");
        for (int j = 0; j < commands_arr[i].num_args; j++) {
            printf("%s\n", commands_arr[i].args[j]);
        }
        printf("\n");
        if (i < num_commands - 1) {
            printf("----------------------------------------"
                   "----------------------------------------\n");
        }
    }

    pid_t pid;
    int status;
    int command_index = 0;
    
    // TODO: CURRENTLY THIS ONLY RUNS THE FIRST COMMAND (WITH COMMAND_INDEX)
    // FIX THIS WHEN YOU DO THE PIPING BY LOOPING OVER COMMANDS

    // Check if this is an internal change dir command
    if (strcmp(commands_arr[command_index].args[0], "cd") == 0) {
        // TODO: Need to handle errors here
        if (commands_arr[command_index].args[1] == NULL ||
            strcmp(commands_arr[command_index].args[1], "~") == 0) {
            chdir(getenv("HOME"));
        }
        else {
            chdir(commands_arr[command_index].args[1]);
        }
        return;
    }

    // Check if this is an internal exit command
    if (strcmp(commands_arr[command_index].args[0], "exit") == 0) {
        exit(0);
    }

    // If not an internal command, fork
    if ((pid = fork()) < 0) {
        printf("Failed to fork process");
        exit(1);
    }
    else if (pid == 0) { // Child process
        execute_command(commands_arr[command_index]);
        int input_file_desc = commands_arr[command_index].input;
        int output_file_desc = commands_arr[command_index].output;
        close(input_file_desc);
        close(output_file_desc);
    }
    else { // Parent process
        wait(&status);
        printf("go back to main\n");
        return;
    }
}

int main() {
    while (1) {
        shell();
    }
    return 0;
}
