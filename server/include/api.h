#ifndef API_H
#define API_H

#define MAX_PIPE_PATH_LENGTH 40
#define OP_CODE_CONNECT 1
#define OP_CODE_DISCONNECT 2
#define OP_CODE_PLAY 3
#define OP_CODE_BOARD 4

typedef struct {
    int op_code;
    char rep_pipe[MAX_PIPE_PATH_LENGTH];
    char notif_pipe[MAX_PIPE_PATH_LENGTH];
} connect_request_t;

typedef struct {
  int width;
  int height;
  int tempo;
  int victory;
  int game_over;
  int accumulated_points;
  char* data;
} Board;

int create_and_open_reg_fifo(const char *path);
int read_connect_request(int req_fd, connect_request_t *request);
int open_client_pipes(const char *rep_pipe_path, const char *notif_pipe_path, int *rep_fd, int *notif_fd, int max_clients);
char get_input_non_blocking(int req_pipe_fd);
int writeBoardChanges(int notif_pipe_fd, Board board);
void send_error_response(int notif_pipe_fd);

#endif