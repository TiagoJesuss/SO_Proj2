#ifndef THREADS_H
#define THREADS_H

#include "board.h"
#include <pthread.h>
#include "api.h"

typedef struct {
    char path[1024];
    level_info *level_info;
} thread_args_t;

typedef struct {
    board_t *game_board;
    int ghost_index;
    bool *leave_thread;
} ghost_thread_args_t;

typedef struct {
    board_t *game_board;
    int *result;
    bool *leave_thread;
    pthread_rwlock_t *lock;
    int req_pipe_fd;
} pacman_thread_args_t;

typedef struct {
    board_t *game_board;
    bool *leave_thread;
} ncurses_thread_args_t;

typedef struct {
    board_t *game_board;
    bool *leave_thread;
    bool *victory;
    int notif_fd;
    bool game_over;
} screen_thread_args_t;


#endif