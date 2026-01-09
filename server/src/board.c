#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo
    if (pac->waiting > 0) {
        pac->waiting -= 1;
        return VALID_MOVE;        
    }
    pac->waiting = pac->passo;

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    pac->current_move+=1;

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    pthread_rwlock_rdlock(&board->board[new_index].lock);
    if (board->board[new_index].has_portal) {
        pthread_rwlock_unlock(&board->board[new_index].lock);
        pthread_rwlock_wrlock(&board->board[old_index].lock);
        board->board[old_index].content = ' ';
        pthread_rwlock_unlock(&board->board[old_index].lock);
        pthread_rwlock_wrlock(&board->board[new_index].lock);
        board->board[new_index].content = 'P';
        pthread_rwlock_unlock(&board->board[new_index].lock);
        return REACHED_PORTAL;
    } else pthread_rwlock_unlock(&board->board[new_index].lock);

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        return DEAD_PACMAN;
    }

    // Collect points
    pthread_rwlock_rdlock(&board->board[new_index].lock);
    if (board->board[new_index].has_dot) {
        pthread_rwlock_unlock(&board->board[new_index].lock);
        pac->points++;
        pthread_rwlock_wrlock(&board->board[new_index].lock);
        board->board[new_index].has_dot = 0;
        pthread_rwlock_unlock(&board->board[new_index].lock);
    } else pthread_rwlock_unlock(&board->board[new_index].lock);

    pthread_rwlock_wrlock(&board->board[old_index].lock);
    board->board[old_index].content = ' ';
    pthread_rwlock_unlock(&board->board[old_index].lock);
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    pthread_rwlock_wrlock(&board->board[new_index].lock);
    board->board[new_index].content = 'P';
    pthread_rwlock_unlock(&board->board[new_index].lock);

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                int index = get_board_index(board, x, i);
                pthread_rwlock_rdlock(&board->board[index].lock);
                char target_content = board->board[index].content;
                pthread_rwlock_unlock(&board->board[index].lock);
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                int index = get_board_index(board, x, i);
                pthread_rwlock_rdlock(&board->board[index].lock);
                char target_content = board->board[index].content;
                pthread_rwlock_unlock(&board->board[index].lock);
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                int index = get_board_index(board, j, y);
                pthread_rwlock_rdlock(&board->board[index].lock);
                char target_content = board->board[index].content;
                pthread_rwlock_unlock(&board->board[index].lock);
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                int index = get_board_index(board, j, y);
                pthread_rwlock_rdlock(&board->board[index].lock);
                char target_content = board->board[index].content;
                pthread_rwlock_unlock(&board->board[index].lock);
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    pthread_rwlock_wrlock(&board->board[old_index].lock);
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    pthread_rwlock_unlock(&board->board[old_index].lock);
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    pthread_rwlock_wrlock(&board->board[new_index].lock);
    board->board[new_index].content = 'M';
    pthread_rwlock_unlock(&board->board[new_index].lock);
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo
    if (ghost->waiting > 0) {
        ghost->waiting -= 1;
        return VALID_MOVE;
    }
    ghost->waiting = ghost->passo;

    char direction = command->command;
    
    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    ghost->current_move++;
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    pthread_rwlock_rdlock(&board->board[new_index].lock);
    char target_content = board->board[new_index].content;
    pthread_rwlock_unlock(&board->board[new_index].lock);

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    pthread_rwlock_wrlock(&board->board[old_index].lock);
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    pthread_rwlock_unlock(&board->board[old_index].lock);

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    pthread_rwlock_wrlock(&board->board[new_index].lock);
    board->board[new_index].content = 'M';
    pthread_rwlock_unlock(&board->board[new_index].lock);
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    pthread_rwlock_wrlock(&board->board[index].lock);
    board->board[index].content = ' ';
    pthread_rwlock_unlock(&board->board[index].lock);

    // Mark pacman as dead
    pac->alive = 0;
}

// Static Loading
int load_pacman(board_t* board, int points, level_info *info) {
    if (info->has_pacman == 1) {
        board->board[info->pacman_info.pos_y * board->width + info->pacman_info.pos_x].content = 'P'; // Pacman
        board->pacmans[0].pos_x = info->pacman_info.pos_x;
        board->pacmans[0].pos_y = info->pacman_info.pos_y;
        board->pacmans[0].alive = 1;
        board->pacmans[0].points = points;
        board->pacmans[0].passo = info->pacman_info.passo;
        board->pacmans[0].waiting = 0;
        board->pacmans[0].current_move = 0;
        board->pacmans[0].n_moves = info->pacman_info.n_moves;
        for (int j = 0; j < board->pacmans[0].n_moves; j++) {
            board->pacmans[0].moves[j] = info->pacman_info.moves[j];
        }
    } else {
        board->board[1 * board->width + 1].content = 'P'; // Pacman
        board->pacmans[0].pos_x = 1;
        board->pacmans[0].pos_y = 1;
        board->pacmans[0].alive = 1;
        board->pacmans[0].points = points;
    }

    return 0;
}

// Static Loading
int load_ghost(board_t* board, pac_ghost_info *info) {
    for (int i = 0; i < board->n_ghosts; i++) {
        board->board[info[i].pos_y * board->width + info[i].pos_x].content = 'M'; // Monster
        board->ghosts[i].pos_x = info[i].pos_x;
        board->ghosts[i].pos_y = info[i].pos_y;
        board->ghosts[i].passo = info[i].passo;
        board->ghosts[i].waiting = 0;
        board->ghosts[i].current_move = 0;
        board->ghosts[i].n_moves = info[i].n_moves;
        for (int j = 0; j < board->ghosts[i].n_moves; j++) {
            board->ghosts[i].moves[j] = info[i].moves[j];
        }
    }

    return 0;
}

int load_level(board_t *board, int points, level_info *info) {
    board->height = info->height;
    board->width = info->width;
    board->tempo = info->tempo;

    board->n_ghosts = info->n_ghosts;
    board->n_pacmans = 1;

    for (int i = 0; i < board->n_ghosts; i++) {
        strcpy(board->ghosts_files[i], info->ghost_files[i]);
    }

    if (info->has_pacman) strcpy(board->pacman_file, info->pacman_file);

    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    strcpy(board->level_name, info->file_name);
    board->board = info->board;

    for (int i = 0; i < board->width * board->height; i++) {
        pthread_rwlock_init(&board->board[i].lock, NULL);
    }

    load_ghost(board, info->ghosts_info);
    load_pacman(board, points, info);

    return 0;
}

void unload_level(board_t * board) {
    for (int i = 0; i < board->width * board->height; i++) {
        pthread_rwlock_destroy(&board->board[i].lock);
    }
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    fclose(debugfile);
}

void debug(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}