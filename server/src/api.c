#include "api.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

int create_and_open_reg_fifo(const char *path) {
    struct stat st;

    if (stat(path, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            debug("File exists and is not a FIFO\n");
            return -1;
        }
    } else {
        if (mkfifo(path, 0666) < 0 && errno != EEXIST) {
            debug("Error creating FIFO: %s\n", strerror(errno));
            return -1;
        }
    }

    int reg_fd = open(path, O_RDONLY);
    if (reg_fd < 0) {
        debug("Error opening FIFO for reading: %s\n", strerror(errno));
        return -1;
    }

    int dummy_fd = open(path, O_WRONLY | O_NONBLOCK);
    if (dummy_fd < 0) {
        if (errno != ENXIO) {
            debug("Error opening FIFO for writing: %s\n", strerror(errno));
        }
    }

    return reg_fd;
}

int read_connect_request(int req_fd, connect_request_t *request) {
    char buffer[MAX_PIPE_PATH_LENGTH * 2 + 1];
    ssize_t bytes_read = read(req_fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        debug("Error reading from FIFO: %s\n", strerror(errno));
        return -1;
    }
    buffer[bytes_read] = '\0';
    if (buffer[0] != OP_CODE_CONNECT) {
        debug("Invalid operation code: %d\n", buffer[0]);
        return -1;
    }
    sscanf(buffer, "%d %s %s", &request->op_code, request->rep_pipe, request->notif_pipe);
    return 1;
}

int open_client_pipes(const char *rep_pipe_path, const char *notif_pipe_path, int *rep_fd, int *notif_fd) {
    int notif_fd = open(notif_pipe_path, O_WRONLY);
    if (notif_fd < 0) {
        debug("Error opening notification pipe: %s\n", strerror(errno));
        return -1;
    }

    int req_fd = open(rep_pipe_path, O_RDONLY | O_NONBLOCK);
    if (req_fd < 0) {
        debug("Error opening reply pipe: %s\n", strerror(errno));
        close(notif_fd);
        return -1;
    }

    int response[2] = {OP_CODE_CONNECT, 0};
    ssize_t bytes_written = write(notif_fd, response, sizeof(response));
    if (bytes_written != sizeof(response)) {
        debug("Error writing to notification pipe: %s\n", strerror(errno));
        close(notif_fd);
        close(req_fd);
        return -1;
    }

    *rep_fd = req_fd;
    *notif_fd = notif_fd;
    
    return 0;
}

char get_input_non_blocking(int req_pipe_fd) {
    char buffer[3];
    ssize_t bytes_read = read(req_pipe_fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return '\0'; // No input available
        } else if (atoi(buffer[0]) != OP_CODE_PLAY) {
            debug("Invalid operation code: %d\n", buffer[0]);
            return '\0';
        } else {
            debug("Error reading from reply pipe: %s\n", strerror(errno));
            return '\0';
        }
    }
    return buffer[1];
}
