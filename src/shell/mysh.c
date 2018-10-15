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

void malloc_error() {
    fprintf(stderr, "Memory allocation failed.  Exiting.\n");
    exit(1);
}

TokenPair tokenize(char *command) {
    int start_index = 0;
    int end_index = 0;
    int in_quotes = 0; // 1 if current character is within quotes, else 0
    // Max number of tokens is MAX_COMMAND_LENGTH
    char **tokens = (char **) malloc(MAX_COMMAND_LENGTH * sizeof(char *));
    if (tokens == NULL) {
        malloc_error();
    }
    int num_tokens = 0;
    int i = 0;
    while (i < strlen(command)) {
        // If character is in quotes, ignore any redirections or pipes
        if (in_quotes && command[i] != '\"') {
            end_index += 1;
            i++;
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
                if (tokens[num_tokens] == NULL) {
                    free(tokens);
                    malloc_error();
                }
                strncpy(tokens[num_tokens], command + start_index,
                    end_index - start_index);
                // Need to manually null terminate these strings
                tokens[num_tokens][end_index - start_index] = '\0';
                num_tokens += 1;
            }

            // Also tokenize the redirects and pipes
            if (command[i] == '>' || command[i] == '<' || command[i] == '|') {
                // Find instances of >>
                if (i < strlen(command) - 1 && command[i + 1] == '>') {
                    tokens[num_tokens] = (char *) malloc (3 * sizeof(char));
                    if (tokens[num_tokens] == NULL) {
                        free(tokens);
                        malloc_error();
                    }
                    strncpy(tokens[num_tokens], command + i, 2);
                    // Need to manually null terminate these strings
                    tokens[num_tokens][2] = '\0';
                    i++;
                    end_index++;
                }
                else {
                    tokens[num_tokens] = (char *) malloc (2 * sizeof(char));
                    if (tokens[num_tokens] == NULL) {
                        free(tokens);
                        malloc_error();
                    }
                    strncpy(tokens[num_tokens], command + i, 1);
                    // Need to manually null terminate these strings
                    tokens[num_tokens][1] = '\0';
                }
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
        i++;
    }
    TokenPair pair;
    pair.num_tokens = num_tokens;
    pair.tokens = tokens;
    return pair;
}

Commands generate_commands(TokenPair pair) {
    Commands commands;
    // Something to return if the files don't exist or something else fails
    Commands null;
    null.commands = (Command *) NULL;
    null.num_commands = 0;
    commands.num_commands = 1;
    for (int i = 0; i < pair.num_tokens; i++) {
        if (strcmp(pair.tokens[i], "|") == 0) {
            commands.num_commands += 1;
        }
    }
    commands.commands = (Command *)
                        malloc(commands.num_commands * sizeof(Command));

    if (commands.commands == NULL) {
        malloc_error();
    }
    
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
                 || strcmp(pair.tokens[i], ">") == 0
                 || strcmp(pair.tokens[i], ">>") == 0) {
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
        if (commands.commands[i].args == NULL) {
            for (int j = 0; j < i; j++) {
                free(commands.commands[j].args);
            }
            free(commands.commands);
            malloc_error();
        }
    }
    int k = 0;
    int l = 0;
    // 1 if after < else 0
    int input_redirection = 0;
    // 1 if after >, 2 if after >>, else 0
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
        else if (strcmp(pair.tokens[i], ">>") == 0) {
            input_redirection = 0;
            output_redirection = 2;
        }
        else if (input_redirection || output_redirection) {
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
            char filename[len + 1];
            strncpy(filename, src, len);
            filename[len] = '\0';

            // Just set the input or output to the file descriptor and change
            // stdin or stdout later when executing the process
            int in_fd, out_fd;
            if (input_redirection) {
                in_fd = open(filename, O_RDONLY, S_IRUSR | S_IWUSR);
                if (in_fd < 0) {// An error occured
                    perror(filename);
                    return null;
                }
                commands.commands[k].input = in_fd;
            }
            if (output_redirection == 1) {
                out_fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY,
                    S_IRUSR | S_IWUSR);
                if (out_fd < 0) {// An error occured
                    perror(filename);
                    return null;
                }
                commands.commands[k].output = out_fd;
            }
            else if (output_redirection == 2) {
                out_fd = open(filename, O_CREAT | O_APPEND | O_WRONLY,
                    S_IRUSR | S_IWUSR);
                if (out_fd < 0) {// An error occured
                    perror(filename);
                    return null;
                }
                commands.commands[k].output = out_fd;
            }
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
            
            if (commands.commands[k].args[l] == NULL) {
                for (int m = 0; m < commands.num_commands; m++) {
                    free(commands.commands[m].args);
                }
                free(commands.commands);
                malloc_error();
            }
            
            strncpy(commands.commands[k].args[l], src, len);
            commands.commands[k].args[l][len] = '\0';
            l += 1;
        }
    }
    commands.commands[k].args[l] = (char *) NULL;

    return commands;
}

int execute_command(Command command) {
    if (command.input != STDIN_FILENO) {
        dup2(command.input, STDIN_FILENO);
        close(command.input);
    }
    if (command.output != STDOUT_FILENO) {
        dup2(command.output, STDOUT_FILENO);
        close(command.output);
    }
    execvp(command.args[0], command.args);
    perror(command.args[0]);
    return 0;
}

void shell() {
    // Print prompt
    char cwd[MAX_PATH_SIZE];
    getcwd(cwd, sizeof(cwd));
    struct passwd *pwuid = getpwuid(getuid());
    if (pwuid == NULL) {
        perror("getpwuid()");
        exit(1);
    }
    printf("%s:%s> ", pwuid->pw_name, cwd);
    char command [MAX_COMMAND_LENGTH];
    int num_tokens;
    char **tokens;
    int total_num_tokens = 0; // handle several line inputs
    char **all_tokens;
    int num_lines = 0;
    int ends_in_backslash = 2;
    while (ends_in_backslash) {
        if (ends_in_backslash == 1) {
            printf("> ");
        }
        fgets(command, MAX_COMMAND_LENGTH, stdin);
        num_lines += 1;
        TokenPair temp_pair = tokenize(command);
        num_tokens = temp_pair.num_tokens;
        tokens = temp_pair.tokens;
        if (num_tokens == 0) {
            break;
        }
        if (strcmp(tokens[num_tokens - 1], "\\") == 0) {
            ends_in_backslash = 1;
            num_tokens -= 1; // ignore the backslash now
        }
        else {
            ends_in_backslash = 0;
        }
        // Sum up total number of tokens
        total_num_tokens += num_tokens;
        if (num_lines == 1) {
            all_tokens = tokens;
        }
        else {
            // Concatenate tokens
            char **og_all_tokens = all_tokens;
            all_tokens = (char **) malloc(total_num_tokens * sizeof(char *));
            memcpy(all_tokens, og_all_tokens, (total_num_tokens - num_tokens)
                    * sizeof(char *));
            memcpy(all_tokens + (total_num_tokens - num_tokens), tokens,
                   num_tokens * sizeof(char *));
            for (int i = 0; i < total_num_tokens - num_tokens; i++) {
                free(og_all_tokens[i]);
            }
            free(og_all_tokens);
            for (int i = 0; i < num_tokens; i++) {
                free(tokens[i]);
            }
            free(tokens);
        }
    }

    TokenPair pair;
    pair.num_tokens = total_num_tokens;
    pair.tokens = all_tokens;

    Commands commands = generate_commands(pair);
    for (int i = 0; i < total_num_tokens; i++) {
        free(all_tokens[i]);
    }
    free(all_tokens);

    // Check if there were any errors when creating the structs
    if (commands.commands == NULL || commands.num_commands == 0) {
        return;
    }
    int num_commands = commands.num_commands;
    Command *commands_arr = commands.commands;

    pid_t pid;
    int status;
    int command_index = 0;

    // No piping
    if (num_commands == 1) {
        // If no commands were entered
        if (commands_arr[command_index].args[0] == NULL) {
            return;
        }
        // Check if this is an internal change dir command
        if (strcmp(commands_arr[command_index].args[0], "cd") == 0) {
            // Handle errors here
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
            perror("fork()");
            exit(1);
        }
        else if (pid == 0) { // Child process
            execute_command(commands_arr[command_index]);
        }
        else { // Parent process
            wait(&status);
            return;
        }
    }
    // Need to loop over commands and pipe
    else {
        int fd[2];
        for (command_index = 0; command_index < num_commands; command_index++) {
            // There was an error opening the pipe
            if (pipe(fd) == -1) {
                perror("pipe()");
                exit(1);
            }
            
            /* This command is in the middle so reads from old pipe and
               writes to a different new pipe. */
            // This is not the last command
            if (command_index < num_commands - 1) {
                commands_arr[command_index].output = fd[1];
                commands_arr[command_index + 1].input = fd[0];
            }
            
            // Fork and execute the command
            if ((pid = fork()) < 0) {
                perror("fork()");
                exit(1);
            }
            else if (pid == 0) { // Child process
                close(fd[0]);
                execute_command(commands_arr[command_index]);
                // Only if execute_command failed (already printed error
                // message) - exit out of the child process
                exit(1);
            }
            else { // Parent process
                // Close write for the parent since the shell will
                // never write to the pipe
                close(fd[1]);
                wait(&status);
            }
        }
    }
    
    for (int i = 0; i < num_commands; i++) {
        for (int j = 0; j < commands_arr[i].num_args + 1; j++) {
            free(commands_arr[i].args[j]);
        }
        free(commands_arr[i].args);
    }
    free(commands_arr);
}

int main() {
    while (1) {
        shell();
    }
    return 0;
}
