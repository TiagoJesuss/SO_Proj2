#include "api.h"
#include "board.h"
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
    char op;
    if (read(req_fd, &op, 1)<0){
        debug("Error reading from FIFO: %s\n", strerror(errno));
        return -1;
    }
    if (read(req_fd, &request->rep_pipe, MAX_PIPE_PATH_LENGTH)<0){
        debug("Error reading from FIFO: %s\n", strerror(errno));
        return -1;
    }
    if (read(req_fd, &request->notif_pipe, MAX_PIPE_PATH_LENGTH)<0){
        debug("Error reading from FIFO: %s\n", strerror(errno));
        return -1;
    }
    if (op != OP_CODE_CONNECT) {
        debug("Invalid operation code: %d\n", &op);
        return -1;
    }
    request->op_code = op;
    return 1;
}

int open_client_pipes(const char *rep_pipe_path, const char *notif_pipe_path, int *rep_fd, int *notif_fd) {
    
    int n_fd = open(notif_pipe_path, O_RDWR);
    if (n_fd < 0) { //esta a entrar aqui dentro
        debug("Error opening notification pipe: %s\n", strerror(errno));
        return -1;
    }

    int r_fd = open(rep_pipe_path, O_RDONLY | O_NONBLOCK);
    if (r_fd < 0) {
        debug("Error opening reply pipe: %s\n", strerror(errno));
        close(n_fd);
        return -1;
    }

    int response[2] = {OP_CODE_CONNECT, 0};
    ssize_t bytes_written = write(n_fd, response, sizeof(response));
    
    fprintf(stderr, "escreveu\n");
    if (bytes_written != sizeof(response)) {
        debug("Error writing to notification pipe: %s\n", strerror(errno));
        close(n_fd);
        close(r_fd);
        return -1;
    }

    *rep_fd = r_fd;
    *notif_fd = n_fd;
    
    return 0;
}

char get_input_non_blocking(int req_pipe_fd) {

    char op;
    char mov;
    if (read(req_pipe_fd, &op, 1)<0){
        debug("Error reading from reply pipe: %s\n", strerror(errno));
    }
    int byteRead = read(req_pipe_fd, &mov, 1);
    if (byteRead<0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
        return '\0'; 
    }
    if (byteRead<0){
        debug("Error reading from reply pipe: %s\n", strerror(errno));
    }
    return '\0';
}

void writeBoardChanges(int notif_pipe_fd, Board board){
    char op = '4';
    if (write(notif_pipe_fd, &op, 1)<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.width, sizeof(int))<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.height, sizeof(int))<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.tempo, sizeof(int))<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.victory, sizeof(int))<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.game_over, sizeof(int))<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.accumulated_points, sizeof(int))<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
    if (write(notif_pipe_fd, &board.data, board.height*board.width)<0){
        debug("Error writing from notif pipe: %s\n", strerror(errno));
    }
}
