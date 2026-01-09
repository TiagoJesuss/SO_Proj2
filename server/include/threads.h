#ifndef THREADS_H
#define THREADS_H

#include "board.h"
#include <pthread.h>
#include "api.h"
#include <semaphore.h>

typedef struct {
    char path[1024];
    level_info *level_info;
} thread_args_t;

typedef struct {
    board_t *game_board;
    int ghost_index;
    int *leave_thread;
} ghost_thread_args_t;

typedef struct {
    board_t *game_board;
    int *result;
    int *leave_thread;
    pthread_rwlock_t *lock;
    int req_pipe_fd;
} pacman_thread_args_t;

typedef struct {
    board_t *game_board;
    int *leave_thread;
} ncurses_thread_args_t;

typedef struct {
    board_t *game_board;
    int *leave_thread;
    int *victory;
    int notif_fd;
    int *game_over;
} screen_thread_args_t;

typedef struct {
    level_info *level_info;
    int n_levels;
    //int *clients;
    int thread_id;
    Queue *queue;
    sem_t *sem_items;
    pthread_mutex_t *mutex_queue;
} worker_thread_args_t;


#endif