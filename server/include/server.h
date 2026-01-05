#ifndef SERVER_H
#define SERVER_H

#define MAX_PATH_LENGTH 40

typedef struct {
    int op_code;
    char rep_pipe[MAX_PATH_LENGTH];
    char notif_pipe[MAX_PATH_LENGTH];
} connect_request_t;

#endif