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
#include <semaphore.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

volatile sig_atomic_t sigusr1_received = 0;

void handle_sigusr1(int signo) {
    (void)signo; // Ignorar warning de unused parameter
    sigusr1_received = 1;
}

Board process_board_to_api(board_t* game_board, int victory, int game_over) {
    Board board_data;
    board_data.width = game_board->width;
    board_data.height = game_board->height;
    board_data.tempo = game_board->tempo;
    board_data.victory = victory;
    board_data.game_over = game_over;
    board_data.accumulated_points = game_board->pacmans[0].points;

    size_t data_size = (size_t)(board_data.width * board_data.height);
    board_data.data = malloc(data_size + 1); 
    for (int i = 0; i < game_board->height; i++) {
        for (int j = 0; j < game_board->width; j++) {
            int index = i * game_board->width + j;
            board_pos_t *pos = &game_board->board[index];
            char ch = pos->content;
            if (ch == ' ') {
                if (pos->has_dot) {
                    ch = '.';
                }
                if (pos->has_portal) {
                    ch = '@';
                }
            }
            if (ch == 'W') ch = '#';
            if (ch == 'P') ch = 'C';
            if (ch == 'G') ch = 'M';
            board_data.data[index] = ch;
        }
    }
    board_data.data[data_size] = '\0'; 

    return board_data;
}

void *screen_thread(void *arg) {
    screen_thread_args_t *args = (screen_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    int *leave_thread = args->leave_thread;
    int tempo = args->game_board->tempo;
    int *victory = args->victory;
    int notif_fd = args->notif_fd;
    int *game_over = args->game_over;
    debug("SCREEN THREAD STARTED\n");
    debug("Victory: %d\nGame Over: %d\n", *victory, *game_over);

    while (*leave_thread == 0) {
        Board board_data = process_board_to_api(game_board, *victory, *game_over);
        if (writeBoardChanges(notif_fd, board_data) < 0) {
            debug("Error writing to notification pipe: %s\n", strerror(errno));
            break;
        }
        sleep_ms(tempo);
    }
    return NULL; 
}

void *ghost_thread(void *arg) {
    ghost_thread_args_t *args = (ghost_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    int ghost_index = args->ghost_index;
    int *leave_thread = args->leave_thread;

    ghost_t* ghost = &game_board->ghosts[ghost_index];
    while (*leave_thread == 0) {
        move_ghost(game_board, ghost_index, &ghost->moves[ghost->current_move % ghost->n_moves]);

        sleep_ms(game_board->tempo);
    }
    return NULL; 
}

void *pacman_thread(void *arg) {
    debug("PACMAN THREAD STARTED\n");
    pacman_thread_args_t *args = (pacman_thread_args_t *)arg;
    board_t *game_board = args->game_board;
    pacman_t *pacman = &game_board->pacmans[0];
    int *result = args->result;
    int *leave_thread = args->leave_thread;
    int req_pipe_fd = args->req_pipe_fd;
    pthread_rwlock_t *lock = args->lock;

    while (pacman->alive) {
        command_t *play;
        command_t c;

        if (pacman->n_moves == 0) { // Se for entrada do usuário
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

        if (play->command == 'Q') {
            pthread_rwlock_wrlock(lock);
            *result = QUIT_GAME;
            *leave_thread = true;
            pthread_rwlock_unlock(lock);
            break;
        }

        int move = move_pacman(game_board, 0, play);
        if (args->game_state != NULL) {
            args->game_state->score = pacman->points;
        }

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

void pacman_thread_args_init(pacman_thread_args_t *args, board_t *game_board, int *result, int *leave_thread, pthread_rwlock_t *lock, int req_pipe_fd, game_state_t *game_state) {
    args->game_board = game_board;
    args->result = result;
    args->leave_thread = leave_thread;
    args->lock = lock;
    args->req_pipe_fd = req_pipe_fd;
    args->game_state = game_state;
}

void ghost_thread_args_init(ghost_thread_args_t *args, board_t *game_board, int ghost_index, int *leave_thread) {
    args->game_board = game_board;
    args->ghost_index = ghost_index;
    args->leave_thread = leave_thread;
}

connect_request_t queue_pop(Queue *head) {
    if (head == NULL || head->next == NULL) {
        connect_request_t empty_request = {0};
        return empty_request; // Fila vazia
    }
    Queue *first_node = head->next;
    connect_request_t request = first_node->request;
    head->next = first_node->next;
    free(first_node);
    return request;
}

void *worker_thread(void *arg) {
    worker_thread_args_t *args = (worker_thread_args_t *)arg;
    level_info *level_info = args->level_info;
    int n_levels = args->n_levels;
    int thread_id = args->thread_id;
    Queue *queue = args->queue;
    sem_t *sem_items = args->sem_items;
    pthread_mutex_t *mutex_queue = args->mutex_queue;
    args->game_state->client_id = thread_id;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    int s = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (s != 0) debug("Erro ao mascarar SIGUSR1 na worker thread\n");

    while (true) {
        int client_req_fd = -1;
        int client_notif_fd = -1;
        sem_wait(sem_items);
        debug("Worker thread %d woke up, checking queue\n", thread_id);
        pthread_mutex_lock(mutex_queue);

        connect_request_t request = queue_pop(queue);
        pthread_mutex_unlock(mutex_queue);

        debug("Worker thread %d processing request: %d %s %s\n", thread_id, request.op_code, request.rep_pipe, request.notif_pipe);

        if (open_client_pipes(request.rep_pipe, request.notif_pipe, &client_req_fd, &client_notif_fd) < 0) {
            debug("Error opening client pipes\n");
            continue;
        }

        int accumulated_points = 0;
        int end_game = 0;
        board_t game_board = {0};
        int lvl = 0;
        int result;
        int leave_thread = 0;
        int victory = 0;
        pthread_rwlock_t l = PTHREAD_RWLOCK_INITIALIZER;

        pacman_thread_args_t pacman_args;
        args->game_state->is_active = 1;
        args->game_state->score = 0;
        pacman_thread_args_init(&pacman_args, &game_board, &result, &leave_thread, &l, client_req_fd, args->game_state);
        pthread_t pacman_tid;

        screen_thread_args_t screen_thread_args;
        screen_thread_args.game_board = &game_board;
        screen_thread_args.leave_thread = &leave_thread;
        screen_thread_args.victory = &victory;
        screen_thread_args.notif_fd = client_notif_fd;
        screen_thread_args.game_over = &end_game;
        pthread_t screen_tid;

        ghost_thread_args_t ghost_args[MAX_GHOSTS];
        pthread_t ghost_tids[MAX_GHOSTS];

        while (!end_game) {
            load_level(&game_board, accumulated_points, &level_info[lvl]);
            for (int i = 0; i < game_board.n_ghosts; i++) {
                ghost_thread_args_init(&ghost_args[i], &game_board, i, &leave_thread);
            }
            while(true) {
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
                if (pthread_create(&screen_tid, NULL, screen_thread, &screen_thread_args) != 0) {
                    perror("pthread_create");
                    exit(EXIT_FAILURE);
                }
                pthread_join(pacman_tid, NULL);
                for (int i = 0; i < game_board.n_ghosts; i++) {
                    pthread_join(ghost_tids[i], NULL);
                }
                pthread_join(screen_tid, NULL);
                leave_thread = false;
                if(result == NEXT_LEVEL) {
                    debug("LEVEL COMPLETED\n");
                    lvl++;
                    if (lvl >= n_levels) {
                        victory = 1;
                        end_game = 1;
                        Board board_data = process_board_to_api(&game_board, victory, end_game);
                        if (writeBoardChanges(client_notif_fd, board_data) < 0) {
                            debug("Error writing to notification pipe: %s\n", strerror(errno));
                        }
                    }
                    sleep_ms(game_board.tempo);
                    break;
                }
                if(result == QUIT_GAME) {
                    sleep_ms(game_board.tempo);
                    end_game = 1;

                    Board board_data = process_board_to_api(&game_board, victory, end_game);
                    if (writeBoardChanges(client_notif_fd, board_data) < 0) {
                        debug("Error writing to notification pipe: %s\n", strerror(errno));
                    }
                    free(board_data.data);

                    break;
                }

                accumulated_points = game_board.pacmans[0].points;      
            }
            unload_level(&game_board);
        }
        args->game_state->is_active = 0;
        close(client_req_fd);
        close(client_notif_fd);
    }

    return NULL;
}

void queue_push(Queue *head, connect_request_t request) {
    if (head == NULL) return;

    Queue *new_node = (Queue *)malloc(sizeof(Queue));
    new_node->request = request;
    new_node->next = NULL;

    Queue *current = head;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = new_node;
}

int compare_scores(const void *a, const void *b) {
    game_state_t *gameA = (game_state_t *)a;
    game_state_t *gameB = (game_state_t *)b;
    return gameB->score - gameA->score;
}

void generate_top5_file(game_state_t *games, int max_games) {
    // Criar uma cópia temporária apenas dos jogos ativos para ordenar
    game_state_t *temp_games = malloc(sizeof(game_state_t) * max_games);
    if (!temp_games) return;

    int active_count = 0;
    for (int i = 0; i < max_games; i++) {
        if (games[i].is_active) {
            temp_games[active_count] = games[i];
            active_count++;
        }
    }

    // Ordenar por pontuação
    qsort(temp_games, active_count, sizeof(game_state_t), compare_scores);

    // Escrever no ficheiro
    int fd = open("top5.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buffer[128];
        int limit = active_count < 5 ? active_count : 5;
        
        for (int i = 0; i < limit; i++) {
            int len = sprintf(buffer, "Client ID: %d, Score: %d\n", 
                            temp_games[i].client_id, temp_games[i].score);
            write(fd, buffer, len);
        }
        close(fd);
    }
    
    free(temp_games);
}


int main(int argc, char** argv) {
    if (argc != 4) {
        printf("Usage: %s <level_directory> <max_games> <register_fifo_path>\n", argv[0]);
        return EXIT_FAILURE;
    }
    open_debug_file("debug.log");
    level_info level_info[MAX_LEVELS];
    int n_levels = read_dir(argv[1], level_info);
    int max_games = atoi(argv[2]);
    char *register_fifo_path = argv[3];
    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    sem_t sem_items;
    pthread_mutex_t mutex_queue;

    sem_init(&sem_items, 0, 0);
    pthread_mutex_init(&mutex_queue, NULL);


    int reg_pipe_fd = create_and_open_reg_fifo(register_fifo_path);
    if (reg_pipe_fd < 0) {
        close_debug_file();
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sa.sa_flags = 0; // Importante para o read ser interrompido
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    Queue *head = (Queue*)malloc(sizeof(Queue));
    head->request = (connect_request_t){0};
    head->next = NULL;

    game_state_t *game_state = calloc(max_games, sizeof(game_state_t));

    pthread_t worker_tid;
    for (int i = 0; i<max_games; i++){
        debug("Creating worker thread %d\n", i);
        worker_thread_args_t worker_args;
        worker_args.level_info = level_info;
        worker_args.n_levels = n_levels;
        worker_args.thread_id = i;
        worker_args.queue = head;
        worker_args.sem_items = &sem_items;
        worker_args.mutex_queue = &mutex_queue;
        worker_args.game_state = &game_state[i];
        if (pthread_create(&worker_tid, NULL, worker_thread, &worker_args) != 0) {
            perror("pthread_create");
            continue;
        }
    }
    debug("Server is running and waiting for clients...\n");
    
    while (1) {
        if (sigusr1_received) {
            generate_top5_file(game_state, max_games);
            sigusr1_received = 0; // Reset da flag
        }
        connect_request_t request;
        if (read_connect_request(reg_pipe_fd, &request) < 0) {
            if (errno == EINTR) {
                continue; // Foi interrompido pelo sinal, volta ao início para verificar a flag
            }
            debug("Error reading connect request\n");
            continue;
        }
        debug("Received connection request: rep_pipe=%s, notif_pipe=%s\n", request.rep_pipe, request.notif_pipe);
        pthread_mutex_lock(&mutex_queue);
        queue_push(head, request);
        debug("request: %d %s %s\n", head->request.op_code, head->request.rep_pipe, head->request.notif_pipe);
        pthread_mutex_unlock(&mutex_queue);
        sem_post(&sem_items);
        debug("Client added to the queue\n");
    }
    close(reg_pipe_fd);
    close_debug_file();

    return 0;
}