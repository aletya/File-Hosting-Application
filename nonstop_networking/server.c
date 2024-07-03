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
#include <dirent.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/epoll.h>
#include "dictionary.h"
#include "common.h"
#include "vector.h"

static int serverSocket, bytes_read, epollfd;
static char buffer[16384];
static char action[100];
static char filename[300];
static dictionary* dict;
static char* dir;
static vector* v;

/*
    RESPONSE\n
    [Error Message]\n
    [File size][Binary Data]
*/

size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

struct request_status {
    //size_t offset; //maybe unnecessary since file pointers move automatically
    verb request_type;
    bool header_parsed;
    int file_fd; //for PUT requests. Unnecessary for GET requests
    size_t file_size; //for PUT requests
    size_t total_bytes_read; //for PUT requests
    char *filename; //for PUT requests
} typedef request_status;


bool vectorContainsFile(char* real_file_name) {
    for(size_t i = 0; i < vector_size(v); i++) {
        if(strcmp(vector_get(v, i), real_file_name) == 0)
            return true;
    }
    return false;
}

void endClient(int client_fd) {
    request_status *req_status;
    req_status = dictionary_get(dict, (void *)(&client_fd));
    if(req_status->file_fd != -1)
        close(req_status->file_fd);
    if(req_status->filename)
        free(req_status->filename);
    free(req_status);
    dictionary_remove(dict, (void *)(&client_fd));
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    //printf("CLOSED CLIENT\n");
}

void putRequestFail(int client_fd) {
    request_status *req_status;
    req_status = dictionary_get(dict, (void *)(&client_fd));
    remove(req_status->filename);
}

int processClient(int client_fd) {
    //printf("trying to process event\n");
    /*
    VERB [filename]\n
    [File size][Binary Data]
    */
    request_status *req_status;
    if(dictionary_contains(dict, (void *)(&client_fd))) {
        //printf("CONTAINS CLIENT!?\n");
        req_status = dictionary_get(dict, (void *)(&client_fd));
    } else {
        //printf("making new req_status\n");
        req_status = malloc(sizeof(request_status));
        req_status->header_parsed = false;
        req_status->file_fd = -1;
        req_status->filename = NULL;
        dictionary_set(dict, (void *)(&client_fd), req_status);
    }

    if(!req_status->header_parsed) {
        size_t offset = 0;
        while(offset == 0 || buffer[offset - 1] != '\n') {
            if(read(client_fd, buffer + offset, 1) == 1)
                offset++;
        }
        buffer[offset] = '\0';
        offset = 0;
        sscanf(buffer, "%s", action);// %s\n
        offset += strlen(action);
        if(strcmp(action, "GET") && strcmp(action, "PUT") && strcmp(action, "DELETE") && strcmp(action, "LIST")) {
            //MALFORMED REQUEST
            write_loop(client_fd, "ERROR\n", 6);
            write_loop(client_fd, err_bad_request, strlen(err_bad_request));
            endClient(client_fd);
            return -1;
        }
        req_status->header_parsed = 1;
        if (strcmp(action, "LIST") == 0) {
            req_status->request_type =  LIST;
            req_status->header_parsed = true;
            //DONE READING, finish request
        } else if (strcmp(action, "PUT") == 0) {
            //printf("PUT REQUEST\n");
            req_status->request_type = PUT;
            req_status->total_bytes_read = 0;
            sscanf(buffer + offset, " %s\n", filename);
            //printf("filename: %s\n", filename);
            offset = 0;
            while(offset < sizeof(size_t)) {
                if(read(client_fd, buffer + offset, 1) == 1)
                    offset++;
            }
            //printf("offset is at %zu\n", offset);
            memcpy(&req_status->file_size, buffer, sizeof(size_t));
            //printf("filesize: %zu\n", req_status->file_size);
            req_status->filename = strdup(filename);
            req_status->file_fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0777);
        } else if (strcmp(action, "GET") == 0) {
            req_status->request_type =  GET;
            sscanf(buffer + offset, " %s\n", filename);
            req_status->file_fd = open(filename, O_RDONLY);
            //DONE READING, finish request
        } else if (strcmp(action, "DELETE") == 0) {
            req_status->request_type = DELETE;
            sscanf(buffer + offset, " %s\n", filename);
            //DONE READING, finish request
        }
    } else {
        printf("header already parsed\n");
        //printf("%zu bytes already read/written\n", req_status->total_bytes_read);
    }

    //GET
    if(req_status->request_type == GET) {
        //one and done (writing)
        struct stat st;
        if (stat(filename, &st) == 0) {
            req_status->file_size = (size_t)st.st_size;
        } else {
            //file doesnt exist
            write_loop(client_fd, "ERROR\n", 6);
            write_loop(client_fd, err_no_such_file, strlen(err_no_such_file));
            endClient(client_fd);
            return -1;
        }
        write_loop(client_fd, "OK\n", 3);
        write_loop(client_fd, (char *)&req_status->file_size, sizeof(size_t));
        size_t total_bytes_written = 0;
        while(total_bytes_written < req_status->file_size) {
            bytes_read = read(req_status->file_fd, buffer, 16384);
            size_t bytes_written = write_loop(client_fd, buffer, bytes_read);
            total_bytes_written += bytes_written;
            if(total_bytes_written == req_status->file_size)
                break;
        }
        endClient(client_fd);
        return 0;
    }

    //PUT
    if(req_status->request_type == PUT) {
        while(true) {
            int client_done = 1;
            bytes_read = server_read_loop(client_fd, buffer, 16384, &client_done);
            if(bytes_read == 0 && client_done) {
                break;
            }
            req_status->total_bytes_read += bytes_read;
            write(req_status->file_fd, buffer, bytes_read);
            if (bytes_read < 16384 && !client_done) {
                return 0;
            }
            if(req_status->total_bytes_read > req_status->file_size) {
                printf("Too many bytes\n");
                write_loop(client_fd, "ERROR\n", 6);
                write_loop(client_fd, err_bad_file_size, strlen(err_bad_file_size));
                putRequestFail(client_fd);
                endClient(client_fd);
                return -1;
            }
        }
        if(req_status->total_bytes_read < req_status->file_size) {
            //recieved too little data
            printf("recieved TOO FEW bytes\n");
            printf("total bytes read: %zu, expected bytes: %zu\n", req_status->total_bytes_read, req_status->file_size);
            write_loop(client_fd, "ERROR\n", 6);
            write_loop(client_fd, err_bad_file_size, strlen(err_bad_file_size));
            //putRequestFail(client_fd);
            endClient(client_fd);
            return -1;
        } else if(req_status->total_bytes_read == req_status->file_size){//should be good?
            //printf("Successful PUT\n");
            if(!vectorContainsFile(req_status->filename))
                vector_push_back(v, req_status->filename);
            write_loop(client_fd, "OK\n", 3);
            endClient(client_fd);
        }
        return 0;
    }

    //DELETE
    if(req_status->request_type == DELETE) {
        //one and done
        if(remove(filename) != 0) {
            write_loop(client_fd, "ERROR\n", 6);
            write_loop(client_fd, err_no_such_file, strlen(err_no_such_file));
            endClient(client_fd);
            return -1;
        } else {
            for(size_t i = 0; i < vector_size(v); i++) {
                if(strcmp(vector_get(v, i), filename) == 0) {
                    vector_erase(v, i);
                    break;
                }
            }
            write_loop(client_fd, "OK\n", 3);
            endClient(client_fd);
            return 0;
        }
    }

    //LIST
    if(req_status->request_type == LIST) {
        //one and done
        size_t total_size = 0;
        if(vector_size(v) == 0) {
            write_loop(client_fd, "OK\n", 3);
            write_loop(client_fd, (char *)&total_size, sizeof(size_t));
            endClient(client_fd);
            return 0;
        }
        for(size_t i = 0; i < vector_size(v); i++) {
            total_size += strlen(vector_get(v, i)) + 1;
        }
        total_size -= 1;
        write_loop(client_fd, "OK\n", 3);
        write_loop(client_fd, (char *)&total_size, sizeof(size_t));
        for(size_t i = 0; i < vector_size(v); i++) {
            write_loop(client_fd, vector_get(v, i), strlen(vector_get(v, i)));
            if(i != vector_size(v) - 1) {
                write_loop(client_fd, "\n", 1);
            }
        }
        endClient(client_fd);
        return 0;
    }
    return -1;
}

int setnonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return 0;
}

void ignore_sigpipe(int sig_num) {
}

void signal_handler(int sig_num) {
    //clear dict
    vector* temp = dictionary_keys(dict);
    for(size_t i = 0; i < vector_size(temp); i++) {
        request_status* req = dictionary_get(dict, vector_get(temp, i));
        free(req);
    }
    dictionary_destroy(dict);
    vector_destroy(temp);

    //delete temp dir
    for(size_t i = 0; i < vector_size(v); i++) {
        remove(vector_get(v, i));
    }
    chdir("..");
    remove(dir);
    
    vector_destroy(v);
    
    //close each connection
    shutdown(serverSocket, SHUT_RDWR);
    close(serverSocket);

    close(epollfd);
    exit(0);
}

int main(int argc, char **argv) {
    // good luck!
    //./server <port>
    //3:41 start
    //3:49 end

    signal(SIGINT, signal_handler);
    signal(SIGPIPE, ignore_sigpipe);

    if(argc != 2) {
        print_server_usage();
        exit(1);
    }

    serverSocket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    //setnonblocking(serverSocket);
    struct addrinfo hints, *p;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int result = getaddrinfo(NULL, argv[1], &hints, &p);
    if(result) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        freeaddrinfo(p);
        exit(1);
    }
    int optval = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(serverSocket, p->ai_addr, p->ai_addrlen) != 0) {
        perror(NULL);
        freeaddrinfo(p);
        exit(1);
    }
    freeaddrinfo(p);

    if (listen(serverSocket, 100) != 0) {//idk how many max clients
        perror(NULL);
        exit(1);
    }


    struct epoll_event ev, events[100];//idk how many max clients
    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // This file object will be `read` from (connect is technically a read operation)
    //makes sure we know to connect to clients when requested
    ev.events = EPOLLIN; //| EPOLLET;//EDGE TRIGGERED, must clear out all calls every time
    ev.data.fd = serverSocket;

    // Add the socket in with all the other fds. Everything is a file descriptor
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSocket, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    dict = int_to_shallow_dictionary_create();
    v = string_vector_create();
    char template[] = "XXXXXX";
    dir = mkdtemp(template);
    print_temp_directory(dir);
    chdir(dir);
    
    //dictionary_destroy(dict);
    int nfds; //number of file descriptors returned by epoll_wait
    while(true) {
        //printf("waiting!\n");
        nfds = epoll_wait(epollfd, events, 100, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(1);
        }
        for(int i = 0; i < nfds; i++) {
            if (events[i].data.fd == serverSocket) {
                int client_fd = accept(serverSocket, NULL, NULL);
                if (client_fd == -1) {
                    perror("accept");
                    exit(1);
                }
                setnonblocking(client_fd);
                ev.events = EPOLLIN; //| EPOLLET;
                ev.data.fd = client_fd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl: client_fd");
                    exit(1);
                }
            } else {
                //process client message on events[i].data.fd
                //fully process a client before moving to the next one
                //printf("epoll: data ready to read!\n");
                //printf("epoll processing client\n");
                processClient(events[i].data.fd);
            }
        }

    }

    return 0;
}