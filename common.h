/**
 * nonstop_networking
 * CS 341 - Spring 2024
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>

#define LOG(...)                      \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0);

typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;

ssize_t write_loop(int socket_fd, const char* msg, size_t count);

ssize_t read_loop(int socket, const char *buffer, size_t count);

ssize_t server_read_loop(int socket, const char *buffer, size_t count, int* client_done);