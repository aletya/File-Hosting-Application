/**
 * nonstop_networking
 * CS 341 - Spring 2024
 */
#include "common.h"
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



ssize_t write_loop(int socket_fd, const char* msg, size_t count) {
    size_t bytes_written = 0;
    while(bytes_written < count) {
        ssize_t return_code = write(socket_fd, msg + bytes_written, count - bytes_written);
        if(return_code == 0) {
            break;
        } else if(return_code > 0) {
            bytes_written += return_code;
        } else if (return_code == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        } else if(return_code == -1 && errno == EINTR) {
            continue;
        } else if(return_code == -1){
            return -1;
        }
    }
    return bytes_written;
}

ssize_t read_loop(int socket, const char *buffer, size_t count) {
    size_t bytes_read = 0;
    while(bytes_read < count) {
        ssize_t return_code = read(socket, (void *) (buffer + bytes_read), count - bytes_read);
        if(return_code == 0) {
            break;
        } else if(return_code > 0) {
            bytes_read += return_code;
        } else if (return_code == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else if(return_code == -1 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return bytes_read;
}

ssize_t server_read_loop(int socket, const char *buffer, size_t count, int* client_done) {
    size_t bytes_read = 0;
    *client_done = 1;
    while(bytes_read < count) {
        ssize_t return_code = read(socket, (void *) (buffer + bytes_read), count - bytes_read);
        if(return_code == 0) {
            break;
        } else if(return_code > 0) {
            bytes_read += return_code;
        } else if (return_code == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            *client_done = 0;
            break;
        } else if(return_code == -1 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return bytes_read;
}