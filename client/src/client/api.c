#include "api.h"
#include "protocol.h"
#include "debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>


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
  strcpy(session.req_pipe_path, req_pipe_path);
  strcpy(session.notif_pipe_path, notif_pipe_path);
  

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
  char op =  OP_CODE_PLAY;
  write(session.req_pipe, &op, 1);
  write(session.req_pipe, &command, sizeof(char));
  // TODO - implement me
}

int pacman_disconnect() {
  // TODO - implement me
  char op = OP_CODE_DISCONNECT;
  write(session.req_pipe, &op, 1);
  close(session.req_pipe);
  close(session.notif_pipe);

  //unlink(session.req_pipe);
  //unlink(session.notif_pipe);
  return 0; 
}

Board receive_board_update(void) {
    // TODO - implement me
  Board cityBoard;
  char op;
  read(session.notif_pipe, &op, 1);
  read(session.notif_pipe, &cityBoard.width, sizeof(int));
  read(session.notif_pipe, &cityBoard.height, sizeof(int));
  read(session.notif_pipe, &cityBoard.tempo, sizeof(int));  
  read(session.notif_pipe, &cityBoard.victory, sizeof(int));
  read(session.notif_pipe, &cityBoard.game_over, sizeof(int));
  read(session.notif_pipe, &cityBoard.accumulated_points, sizeof(int));
  cityBoard.data = (char*)malloc(sizeof(char)*(cityBoard.height*cityBoard.width));
  read(session.notif_pipe, cityBoard.data, cityBoard.height*cityBoard.width);
  //nao esquecer dar free ao data
  return cityBoard;
  
}