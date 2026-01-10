#include "api.h"
#include "protocol.h"
#include "debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>


struct Session {
  int id;
  int req_pipe;
  int notif_pipe;
  char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
};

static struct Session session = {.id = -1};

int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {
  // TODO - implement me
  
  mkfifo(req_pipe_path, 0666);
  mkfifo(notif_pipe_path, 0666);
  //strcpy(session.req_pipe_path, req_pipe_path);
  //strcpy(session.notif_pipe_path, notif_pipe_path);

  memset(session.req_pipe_path, 0, MAX_PIPE_PATH_LENGTH); 
  strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH - 1);

  memset(session.notif_pipe_path, 0, MAX_PIPE_PATH_LENGTH); 
  strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH - 1);
  

  int serverFd = open(server_pipe_path, O_WRONLY);
  if (serverFd < 0) {
    perror("reg open error");
    return EXIT_FAILURE;
  }
  char op = OP_CODE_CONNECT;
  write(serverFd, &op, sizeof(char)); //pode nao conseguir escrever toda a data atencao
  write(serverFd, session.req_pipe_path, MAX_PIPE_PATH_LENGTH);
  write(serverFd, session.notif_pipe_path, MAX_PIPE_PATH_LENGTH);
  //close(serverFd);

  int notFd = open(session.notif_pipe_path, O_RDONLY);
  if (notFd < 0) {
    perror("notif open error");
    return EXIT_FAILURE;
  }
  session.notif_pipe = notFd;
  char buf[2];
  read(notFd, &buf, 2);
  printf("buf : %s", buf);
  if (buf[0]!=op || buf[1]!=0){
    close(notFd);
    return -1;
  }
  
  int reqFd = open(session.req_pipe_path, O_WRONLY);
  if (reqFd < 0) {
    perror("req open error");
    debug("Could not open req pipe\n");
    return EXIT_FAILURE;
  }
  session.req_pipe = reqFd;

  return 0;
}

void pacman_play(char command) {
  char msg[2];
  msg[0] = OP_CODE_PLAY;
  msg[1] = command;
  debug("pacman_play: Command: %c\n", command);
  if (write(session.req_pipe, msg, 2) < 0) {
    debug("Error writing to req pipe: %s\n", strerror(errno));
    return;
  }
  /*
  if (write(session.req_pipe, &op, 1) < 0) {
    debug("Error writing to req pipe: %s\n", strerror(errno));
    return;
  }
  debug("pacman_play: OP CODE sent: %d\n", op);
  if (write(session.req_pipe, &command, 1) < 0) {
    debug("Error writing to req pipe: %s\n", strerror(errno));
    return;
  }
  debug("pacman_play: Command sent: %c\n", command);
  // TODO - implement me
  */
}

int pacman_disconnect() {
  char op = OP_CODE_DISCONNECT;
  debug("pacman_disconnect: Disconnecting...\n");
  if (write(session.req_pipe, &op, 1) < 0) {
    debug("Error writing to req pipe: %s\n", strerror(errno));
  } else {
    debug("pacman_disconnect: OP CODE sent: %d\n", op);
  }
  close(session.req_pipe);
  debug("pacman_disconnect: Req pipe closed\n");
  close(session.notif_pipe); 
  return 0; 
}

Board receive_board_update(void) {
    // TODO - implement me
  Board cityBoard;
  char op;
  if (read(session.notif_pipe, &op, 1) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  if (read(session.notif_pipe, &cityBoard.width, sizeof(int)) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  if (read(session.notif_pipe, &cityBoard.height, sizeof(int)) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  if (read(session.notif_pipe, &cityBoard.tempo, sizeof(int)) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  if (read(session.notif_pipe, &cityBoard.victory, sizeof(int)) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  if (read(session.notif_pipe, &cityBoard.game_over, sizeof(int)) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  if (read(session.notif_pipe, &cityBoard.accumulated_points, sizeof(int)) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    cityBoard.data = NULL;
    return cityBoard;
  }
  cityBoard.data = (char*)malloc(sizeof(char)*(cityBoard.height*cityBoard.width+1));
  if (read(session.notif_pipe, cityBoard.data, cityBoard.height*cityBoard.width) <= 0) {
    debug("Error reading from FIFO: %s\n", strerror(errno));
    free(cityBoard.data); // free memory if read fails
    cityBoard.data = NULL;
    return cityBoard;
  }
  cityBoard.data[cityBoard.height*cityBoard.width] = '\0';
  //nao esquecer dar free ao data
  return cityBoard;
  
}