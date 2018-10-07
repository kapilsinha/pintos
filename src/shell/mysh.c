#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

// We are told to assume the command is less than 1 KiB
#define MAX_COMMAND_LENGTH 1024
// maximum path length for Linux is 4096 chars
#define MAX_PATH_SIZE 4096
// maximum filename length for Linux is 256 chars
#define MAX_FILENAME_SIZE 256

struct Command {
    char input[MAX_FILENAME_SIZE];
    char output[MAX_FILENAME_SIZE];
    char error[MAX_FILENAME_SIZE];
    int num_tokens;
    char **args;
};

struct TokenPair {
    int num_tokens;
    char **tokens;
};

struct TokenPair tokenize(char * command) {
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
                    malloc ((end_index - start_index) * sizeof(char));
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
    struct TokenPair pair;
    pair.num_tokens = num_tokens;
    pair.tokens = tokens;
    return pair;
}

int main() {
    // Print prompt (TODO: fix outputting the username
    // - currently getlogin() returns NULL)
    char cwd[MAX_PATH_SIZE];
    getcwd(cwd, sizeof(cwd));
    printf("%s:%s> ", getlogin(), cwd); 

    char command [MAX_COMMAND_LENGTH];
    fgets(command, MAX_COMMAND_LENGTH, stdin);
    
    struct TokenPair pair = tokenize(command);
    int num_tokens = pair.num_tokens;
    char **tokens = pair.tokens;
    printf("Number of tokens: %d\n", num_tokens);
    for (int i = 0; i < num_tokens; i++) {
        printf("%s\n", tokens[i]);
    }
}
