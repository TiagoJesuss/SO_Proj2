#include "board.h"
#include "display.h"
#include "threads.h"
#include "api.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <sys/wait.h>   
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

void *ncurses_thread(void *arg) {
    ncurses_thread_args_t *args = (ncurses_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    pthread_rwlock_t *ncurses_lock = game_board->ncurses_lock;
    bool *leave_thread = args->leave_thread;

    while (*leave_thread == 0) {
        pthread_rwlock_wrlock(ncurses_lock);
        screen_refresh(game_board, DRAW_MENU);
        pthread_rwlock_unlock(ncurses_lock);

        sleep_ms(game_board->tempo);
    }
    return NULL; 
}

Board process_board_to_api(board_t* game_board, bool victory, bool game_over) {
    Board board_data;
    board_data.width = game_board->width;
    board_data.height = game_board->height;
    board_data.tempo = game_board->tempo;
    board_data.victory = victory ? 1 : 0;
    board_data.game_over = game_over ? 1 : 0;
    board_data.accumulated_points = game_board->pacmans[0].points;

    size_t data_size = (size_t)(board_data.width * board_data.height);
    board_data.data = malloc(data_size + 1); 
    for (int i = 0; i < game_board->height; i++) {
        for (int j = 0; j < game_board->width; j++) {
            int index = i * game_board->width + j;
            board_pos_t *pos = &game_board->board[index];
            char ch = pos->content;
            if (pos->has_dot) {
                ch = '.';
            }
            if (pos->has_portal) {
                ch = '@';
            }
            if (ch == ' ') {
                int occupied = 0;
                for (int p = 0; p < game_board->n_pacmans; p++) {
                    pacman_t *pacman = &game_board->pacmans[p];
                    if (pacman->alive && pacman->pos_x == j && pacman->pos_y == i) {
                        ch = 'P';
                        occupied = 1;
                        break;
                    }
                }
                if (!occupied) {
                    for (int g = 0; g < game_board->n_ghosts; g++) {
                        ghost_t *ghost = &game_board->ghosts[g];
                        if (ghost->pos_x == j && ghost->pos_y == i) {
                            ch = 'M';
                            break;
                        }
                    }
                }
            }
            board_data.data[index] = ch;
        }
    }
    board_data.data[data_size] = '\0'; 

    return board_data;
}

void *screen_thread(void *arg) {
    screen_thread_args_t *args = (screen_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    bool *leave_thread = args->leave_thread;
    bool *victory = args->victory;
    int notif_fd = args->notif_fd;
    bool game_over = args->game_over;
    Board board_data = process_board_to_api(game_board, *victory, game_over);

    while (!*leave_thread) {
        if (writeBoardChanges(notif_fd, board_data) < 0) {
            debug("Error writing to notification pipe: %s\n", strerror(errno));
            break;
        }
        sleep_ms(game_board->tempo);
    }
    return NULL; 
}

void *ghost_thread(void *arg) {
    ghost_thread_args_t *args = (ghost_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    int ghost_index = args->ghost_index;
    bool *leave_thread = args->leave_thread;

    ghost_t* ghost = &game_board->ghosts[ghost_index];
    while (*leave_thread == 0) {
        move_ghost(game_board, ghost_index, &ghost->moves[ghost->current_move % ghost->n_moves]);

        sleep_ms(game_board->tempo);
    }
    return NULL; 
}

void *pacman_thread(void *arg) {
    pacman_thread_args_t *args = (pacman_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    pacman_t *pacman = &game_board->pacmans[0];
    int *result = args->result;
    bool *leave_thread = args->leave_thread;
    int req_pipe_fd = args->req_pipe_fd;
    pthread_rwlock_t *lock = args->lock;

    while (pacman->alive) {
        command_t *play;
        command_t c;

        if (pacman->n_moves == 0) { // Se for entrada do usuário
            /*
            pthread_rwlock_rdlock(ncurses_lock);
            c.command = get_input();
            pthread_rwlock_unlock(ncurses_lock);
            */
            c.command = get_input_non_blocking(req_pipe_fd);
            if (c.command == '\0') {
                continue; // Sem entrada, continua
            }

            c.turns = 1;
            play = &c;
        } else { // Movimentos predefinidos
            c.command = get_input();
            if (c.command == 'Q'){
                pthread_rwlock_wrlock(lock);
                *result = QUIT_GAME;
                *leave_thread = true;
                pthread_rwlock_unlock(lock);
                break;
                
            }
            play = &pacman->moves[pacman->current_move % pacman->n_moves];
            debug("MOVE %d - %c\n", pacman->current_move % pacman->n_moves, play->command);
        }

        debug("KEY %c\n", play->command);

        if (play->command == 'Q') {
            pthread_rwlock_wrlock(lock);
            *result = QUIT_GAME;
            *leave_thread = true;
            pthread_rwlock_unlock(lock);
            break;
        }

        int move = move_pacman(game_board, 0, play);

        if (move == REACHED_PORTAL) {
            pthread_rwlock_wrlock(lock);
            *result = NEXT_LEVEL;
            *leave_thread = true;
            pthread_rwlock_unlock(lock);
            break; // Pacman venceu
        }

        if (move == DEAD_PACMAN) {
            pthread_rwlock_wrlock(lock);
            *result = QUIT_GAME;
            *leave_thread = true;
            pthread_rwlock_unlock(lock);
            break; // Pacman morreu
        }

        sleep_ms(game_board->tempo); // Aguarda o tempo definido
    }
    if (!pacman->alive) {
        pthread_rwlock_wrlock(lock);
        *result = QUIT_GAME;
        *leave_thread = true;
        pthread_rwlock_unlock(lock);
    }

    return NULL;
}


void process_board(board_pos_t *board, char *board_str, int height, int width) {
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int index = i * width + j;
            char ch = board_str[index];
            switch (ch) {
                case 'X': // Wall
                    board[index].content = 'W';
                    board[index].has_dot = 0;
                    board[index].has_portal = 0;
                    break;
                case 'o': // Free space
                    board[index].content = ' ';
                    board[index].has_dot = 1;
                    board[index].has_portal = 0;
                    break;
                case '@': // Portal
                    board[index].content = ' ';
                    board[index].has_dot = 0;
                    board[index].has_portal = 1;
                    break;
            }

        }
    }
}

char* readFile (char *file) {
    int f = open(file, O_RDONLY);
    if (f < 0) {
        exit(EXIT_FAILURE);
    }
    ssize_t bytes_read;
    char buffer[1024];
    size_t fileSize = 0;
    char *fileContent = NULL;
    while ((bytes_read = read(f, buffer, sizeof(buffer)-1)) > 0) {
        buffer[bytes_read] = '\0'; // Garante que o buffer seja uma string válida

        fileContent = realloc(fileContent, fileSize + bytes_read + 1);
        if (fileContent == NULL) {
            close(f);
            exit(EXIT_FAILURE);
        }

        memcpy(fileContent + fileSize, buffer, bytes_read + 1);
        fileSize += bytes_read;
    }
    close(f);
    return fileContent;
}

void build_command(command_t *command, char *line) {
    sscanf(line, "%c", &command->command);
    if (command->command == 'T') {
        sscanf(line, "T %d", &command->turns_left);
    }
    command->turns = 1;
}

char* getFileName(char *file) {
    char *filename = strrchr(file, '/');
    if (filename != NULL) {
        return filename + 1;
    }
    return file;
}

pac_ghost_info getPacGhostInfo(char *file) { //esqueceu se de atribuir nmoves
    pac_ghost_info info;
    char *fileInfo = readFile(file);
    strncpy(info.file_name, getFileName(file), MAX_FILENAME - 1);
    char *saveptr_line; // Estado para strtok_r
    char *line = strtok_r(fileInfo, "\n", &saveptr_line);
    while (line != NULL) {
        if (strncmp(line, "#", 1) == 0) {
            line = strtok_r(NULL, "\n", &saveptr_line);
            continue;
        } else if (strncmp(line, "PASSO", 5) == 0) {
            sscanf(line, "PASSO %d", &info.passo);
        } else if (strncmp(line, "POS", 3) == 0) {
            sscanf(line, "POS %d %d", &info.pos_x, &info.pos_y);
        } else {
            int n_moves = 0;
            while (line != NULL && n_moves < MAX_MOVES) {
                build_command(&info.moves[n_moves], line);
                n_moves++;
                line = strtok_r(NULL, "\n", &saveptr_line);
            }
            info.n_moves = n_moves;
            break;
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
    }
    free(fileInfo);
    return info;
}

char* getPath(char *base_path, char *file_name) {
    char *last_slash = strrchr(base_path, '/');
    char *path;
    if (last_slash != NULL) {
        size_t dir_length = last_slash - base_path + 1; // +1 para incluir a barra
        path = malloc(dir_length + strlen(file_name) + 1); // +1 para o terminador nulo
        strncpy(path, base_path, dir_length);
        path[dir_length] = '\0'; // Adiciona o terminador nulo
        strcat(path, file_name);
    } else {
        path = malloc(strlen(file_name) + 1);
        strcpy(path, file_name);
    }
    return path;
}

level_info getLevelInfo(char *level_file) {
    level_info info;
    info.has_pacman = 0;
    char* fileInfo = readFile(level_file);
    strncpy(info.file_name, getFileName(level_file), MAX_FILENAME - 1);
    char *board = NULL;
    char *saveptr_line; // Estado para strtok_r
    char *line = strtok_r(fileInfo, "\n", &saveptr_line);
    while (line != NULL) {
        if (strncmp(line, "DIM", 3) == 0) {
            sscanf(line, "DIM %d %d", &info.width, &info.height);
            info.board = malloc(sizeof(board_pos_t) * (info.width * info.height + 1));
            board = malloc(info.width * info.height + 1);
            board[0] = '\0';
        } else if (strncmp(line, "TEMPO", 5) == 0) {
            sscanf(line, "TEMPO %d", &info.tempo);
        } else if (strncmp(line, "PAC", 3) == 0) {
            sscanf(line, "PAC %s", info.pacman_file);
            char *path = getPath(level_file, info.pacman_file);
            info.pacman_info = getPacGhostInfo(path);
            free(path);
            info.has_pacman = 1;
        } else if (strncmp(line, "MON", 3) == 0) {
            int ghost_index = 0;
            char *saveptr_token; // Estado para strtok_r dentro da linha
            char *token = strtok_r(line + 4, " ", &saveptr_token);
            while (token != NULL) {
                if (ghost_index >= MAX_GHOSTS) { 
                    break;
                }
                strncpy(info.ghost_files[ghost_index], token, MAX_FILENAME - 1);
                info.ghost_files[ghost_index][MAX_FILENAME - 1] = '\0'; 
                char *path = getPath(level_file, info.ghost_files[ghost_index]);
                info.ghosts_info[ghost_index] = getPacGhostInfo(path);
                free(path);
                ghost_index++;
                token = strtok_r(NULL, " ", &saveptr_token);
            }
            info.n_ghosts = ghost_index;
        } else if (strncmp(line, "#", 1) == 0) {
            line = strtok_r(NULL, "\n", &saveptr_line);
            continue;
        } else {
            strcat(board, line);
            line = strtok_r(NULL, "\n", &saveptr_line);
            while (line != NULL) {
                strcat(board, line);
                line = strtok_r(NULL, "\n", &saveptr_line);
            }
            process_board(info.board, board, info.height, info.width);
            free(board);
            break;
            
        }
        line = strtok_r(NULL, "\n", &saveptr_line);
        
    }
    free(fileInfo);
    return info;
}

void *read_file_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;

    *(args->level_info) = getLevelInfo(args->path);

    free(args);
    return NULL;
}

int read_dir(char *argv, level_info *level_info) {
    DIR *dir = opendir(argv);
    if (dir == NULL) {
        return 1;
    }
    struct dirent *entry;
    int x = 0;

    pthread_t threads[MAX_LEVELS];
    int thread_count = 0;

    while ((entry = readdir(dir)) != NULL) { // Lê cada ficheiro na diretoria
        const char *dot = strrchr(entry->d_name, '.');
        char extension[4] = "";
        if (dot != NULL && *(dot + 1) != '\0') {
            strncpy(extension, dot + 1, sizeof(extension) - 1); 
            extension[sizeof(extension) - 1] = '\0';
        }
        if (extension[0] == 'l') {
            thread_args_t *args = malloc(sizeof(thread_args_t));
            sprintf(args->path, "%s/%s", argv, entry->d_name);
            args->level_info = &level_info[x];
            
            if (pthread_create(&threads[thread_count], NULL, read_file_thread, args) != 0) {
                perror("pthread_create");
                free(args);
                continue;
            }
            thread_count++;
            x++;
        }
        
    }
    
    for (int j = 0; j < thread_count; j++) {
        pthread_join(threads[j], NULL);
    }
    
    closedir(dir);
    return x;
}

void pacman_thread_args_init(pacman_thread_args_t *args, board_t *game_board, int *result, bool *leave_thread, pthread_rwlock_t *lock) {
    args->game_board = game_board;
    args->result = result;
    args->leave_thread = leave_thread;
    args->lock = lock;
}

void ghost_thread_args_init(ghost_thread_args_t *args, board_t *game_board, int ghost_index, bool *leave_thread) {
    args->game_board = game_board;
    args->ghost_index = ghost_index;
    args->leave_thread = leave_thread;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        printf("Usage: %s <level_directory> <max_games> <register_fifo_path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    open_debug_file("debug.log");
    level_info level_info[MAX_LEVELS];
    int n_levels = read_dir(argv[1], level_info);
    //int max_games = atoi(argv[2]);
    char *register_fifo_path = argv[3];
    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    //terminal_init(); -> ncurses vai ser do lado do cliente
    pthread_rwlock_t l = PTHREAD_RWLOCK_INITIALIZER;
    pthread_rwlock_t ncurses_lock = PTHREAD_RWLOCK_INITIALIZER;

    int reg_pipe_fd = create_and_open_reg_fifo(register_fifo_path);
    if (reg_pipe_fd < 0) {
        close_debug_file();
        return EXIT_FAILURE;
    }
    /*
    if (signal(SIGUSR1, handler)==){
        x = 1;
        int logFd = open("topFiveClients.txt", O_WRONLY | O_CREAT, 0644);
        getTopFive;
        write(logFd, );
        close(logFd);
    }
    */
    int client_req_fd = -1; 
    int client_notif_fd = -1;
    connect_request_t request;
    if (read_connect_request(reg_pipe_fd, &request) < 0) {
        close(reg_pipe_fd);
        close_debug_file();
        return EXIT_FAILURE;
    }
    if (open_client_pipes(request.rep_pipe, request.notif_pipe, &client_req_fd, &client_notif_fd) < 0) {
        close(reg_pipe_fd);
        close_debug_file();
        return EXIT_FAILURE;
    }
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board = {0};
    int lvl = 0;
    int result;
    bool leave_thread = false;
    bool victory = false;

    pacman_thread_args_t pacman_args;
    pacman_thread_args_init(&pacman_args, &game_board, &result, &leave_thread, &l);
    pthread_t pacman_tid;

    game_board.ncurses_lock = &ncurses_lock;
    /*
    ncurses_thread_args_t ncurses_thread_args;
    pthread_t ncurses_tid;
    ncurses_thread_args.game_board = &game_board;
    ncurses_thread_args.leave_thread = &leave_thread;
    */

    screen_thread_args_t screen_thread_args;
    screen_thread_args.game_board = &game_board;
    screen_thread_args.leave_thread = &leave_thread;
    screen_thread_args.victory = &victory;
    screen_thread_args.notif_fd = client_notif_fd;
    pthread_t screen_tid;

    ghost_thread_args_t ghost_args[MAX_GHOSTS];
    pthread_t ghost_tids[MAX_GHOSTS];



    while (!end_game) {
        load_level(&game_board, accumulated_points, &level_info[lvl]);
        for (int i = 0; i < game_board.n_ghosts; i++) {
            ghost_thread_args_init(&ghost_args[i], &game_board, i, &leave_thread);
        }
        //draw_board(&game_board, DRAW_MENU); -> vai ser feito do lado do cliente
        //refresh_screen(); -> vai ser feito do lado do cliente
        while(true) {
            /*
            if (pthread_create(&ncurses_tid, NULL, ncurses_thread, &ncurses_thread_args) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }*/
            if (pthread_create(&screen_tid, NULL, screen_thread, &screen_thread_args) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
            if (pthread_create(&pacman_tid, NULL, pacman_thread, &pacman_args) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < game_board.n_ghosts; i++) {
                if (pthread_create(&ghost_tids[i], NULL, ghost_thread, &ghost_args[i]) != 0) {
                    perror("pthread_create");
                    exit(EXIT_FAILURE);
                }
            }
            pthread_join(pacman_tid, NULL);
            for (int i = 0; i < game_board.n_ghosts; i++) {
                pthread_join(ghost_tids[i], NULL);
            }
            pthread_join(screen_tid, NULL);
            //pthread_join(ncurses_tid, NULL);
            leave_thread = false;
            if(result == NEXT_LEVEL) {
                lvl++;
                if (lvl >= n_levels) {
                    //screen_refresh(&game_board, DRAW_WIN);
                    victory = true;
                    end_game = true;
                } else {
                    //screen_refresh(&game_board, DRAW_MENU);
                }
                sleep_ms(game_board.tempo);
                break;
            }
            if(result == QUIT_GAME) {
                //screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }
    
            //screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        //print_board(&game_board);
        unload_level(&game_board);
    }
    
    for (int i = lvl + 1; i < n_levels; i++) {
        free(level_info[i].board);
    }
    close(reg_pipe_fd);

    //terminal_cleanup();

    close_debug_file();

    return 0;
}