/**
 * nonstop_networking
 * CS 341 - Spring 2024
 */
#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>



#include "common.h"

static char buffer[16384];

char **parse_args(int argc, char **argv);
verb check_args(char **args);

void close_connection(int fd, char **args) {
    free(args);
    print_connection_closed();
    close(fd);
    shutdown(fd, SHUT_RDWR);
}

int main(int argc, char **argv) {
    // Good luck!
    char **args = parse_args(argc, argv);

    
    //GET, PUT, LIST or DELETE
    verb request_type = check_args(args);
    (void)request_type;

    //connect to server
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct addrinfo hints, *p;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int result = getaddrinfo(args[0], args[1], &hints, &p);
    if(result) {
        freeaddrinfo(p);
        free(args);
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        exit(1);
    }

    result = connect(socket_fd, p->ai_addr, p->ai_addrlen);
    if(result) {
        freeaddrinfo(p);
        close(socket_fd);
        free(args);
        perror("connect()");
        exit(1);
    }
    freeaddrinfo(p);


    /*
    VERB [filename]\n
    [File size][Binary Data]
    */

    if(request_type == GET) {
        //GET [filename]\n
        char* str;
        asprintf(&str, "GET %s\n", args[3]);
        write_loop(socket_fd, str, strlen(str));
        free(str);
        shutdown(socket_fd, SHUT_WR);

        read(socket_fd, buffer, 1);
        buffer[1] = '\0';
        if(buffer[0] == 'O') {
            read(socket_fd, buffer + 1, 2);
            buffer[3] = '\0';
            if(strcmp(buffer, "OK\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            } else {
                //read file_size
                size_t file_size;
                read(socket_fd, (char *)&file_size, sizeof(size_t));
                //now read from server while writing to the file args[4]
                int output = open(argv[4], O_WRONLY | O_CREAT | O_TRUNC, 0777);
                size_t bytes_read;
                size_t total_bytes_read = 0;
                while((bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1))) {
                    total_bytes_read += bytes_read;
                    if(total_bytes_read > file_size) {
                        print_received_too_much_data();
                        close(output);
                        close_connection(socket_fd, args);
                        exit(1);
                    }
                    //write these directly to the localfile
                    write(output, buffer, bytes_read);
                }
                if(total_bytes_read < file_size) {
                    print_too_little_data();
                    close(output);
                    close_connection(socket_fd, args);
                    exit(1);
                }
                close(output);
            }
        } else if (buffer[0] == 'E') {
            read(socket_fd, buffer + 1, 5);
            buffer[6] = '\0';
            if(strcmp(buffer, "ERROR\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            }
            size_t bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            print_error_message(buffer);
            close_connection(socket_fd, args);
            exit(1);
        } else {
            print_invalid_response();
            close_connection(socket_fd, args);
            exit(1);
        }
    }

    if(request_type == PUT) {
        //PUT [filename]\n
        //[File size][Binary Data]
        //DELETE [filename]\n
        struct stat st;
        size_t file_size;
        if (stat(args[4], &st) == 0) {
            file_size = (size_t)st.st_size;
        } else {
            close_connection(socket_fd, args);
            exit(1);
        }
        char* str;
        asprintf(&str, "PUT %s\n", args[3]);
        write_loop(socket_fd, str, strlen(str));
        free(str);
        write_loop(socket_fd, (char *)&file_size, sizeof(size_t));
        int input = open(argv[4], O_RDONLY);
        size_t total_bytes_written = 0;
        while(total_bytes_written < file_size) {
            size_t bytes_read = read(input, buffer, sizeof(buffer) - 1);
            size_t bytes_written = write_loop(socket_fd, buffer, bytes_read);
            total_bytes_written += bytes_written;
            if(total_bytes_written == file_size)
                break;
        }
        close(input);
        shutdown(socket_fd, SHUT_WR);

        
        read(socket_fd, buffer, 1);
        buffer[1] = '\0';
        if(buffer[0] == 'O') {
            read(socket_fd, buffer + 1, 2);
            buffer[3] = '\0';
            if(strcmp(buffer, "OK\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            } else {
                print_success();
            }
        } else if (buffer[0] == 'E') {
            read(socket_fd, buffer + 1, 5);
            buffer[6] = '\0';
            if(strcmp(buffer, "ERROR\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            }
            size_t bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            print_error_message(buffer);
            close_connection(socket_fd, args);
            exit(1);
        } else {
            print_invalid_response();
            close_connection(socket_fd, args);
            exit(1);
        }
    }

    if(request_type == LIST) {
        //LIST\n
        write_loop(socket_fd, "LIST\n", 5);
        shutdown(socket_fd, SHUT_WR);

        read(socket_fd, buffer, 1);
        buffer[1] = '\0';
        if(buffer[0] == 'O') {
            read(socket_fd, buffer + 1, 2);
            buffer[3] = '\0';
            if(strcmp(buffer, "OK\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            } else {
                //read file_size
                size_t file_size;
                read(socket_fd, (char *)&file_size, sizeof(size_t));
                //now read from server while writing to stdout
                size_t bytes_read;
                size_t total_bytes_read = 0;
                while((bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1))) {
                    total_bytes_read += bytes_read;
                    if(total_bytes_read > file_size) {
                        print_received_too_much_data();
                        close_connection(socket_fd, args);
                        exit(1);
                    }
                    //write these directly to the localfile
                    fwrite(buffer, 1, bytes_read, stdout);
                }
                if(total_bytes_read < file_size) {
                    print_too_little_data();
                    close_connection(socket_fd, args);
                    exit(1);
                }
            }
        } else if (buffer[0] == 'E') {
            read(socket_fd, buffer + 1, 5);
            buffer[6] = '\0';
            if(strcmp(buffer, "ERROR\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            }
            size_t bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            print_error_message(buffer);
            close_connection(socket_fd, args);
            exit(1);
        } else {
            print_invalid_response();
            close_connection(socket_fd, args);
            exit(1);
        }

    }

    if(request_type == DELETE) {
        //DELETE [filename]\n
        char* str;
        asprintf(&str, "DELETE %s\n", args[3]);
        write_loop(socket_fd, str, strlen(str));
        free(str);
        shutdown(socket_fd, SHUT_WR);

        read(socket_fd, buffer, 1);
        buffer[1] = '\0';
        if(buffer[0] == 'O') {
            read(socket_fd, buffer + 1, 2);
            buffer[3] = '\0';
            if(strcmp(buffer, "OK\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            } else {
                print_success();
            }
        } else if (buffer[0] == 'E') {
            read(socket_fd, buffer + 1, 5);
            buffer[6] = '\0';
            if(strcmp(buffer, "ERROR\n") != 0) {
                print_invalid_response();
                close_connection(socket_fd, args);
                exit(1);
            }
            size_t bytes_read = read(socket_fd, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            print_error_message(buffer);
            close_connection(socket_fd, args);
            exit(1);
        } else {
            print_invalid_response();
            close_connection(socket_fd, args);
            exit(1);
        }   
    }

    free(args);
    close(socket_fd);
    shutdown(socket_fd, SHUT_RDWR);
    return 0;

    //send request
    //prints out any error messages from the server to stdout using print_error_message
    //print_invalid_response when client/server does not send a valid response or header as per the protocol
    //described in the docs
}

/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}
