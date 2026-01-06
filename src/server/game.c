#define _DEFAULT_SOURCE
#include "board.h"
#include "protocol.h"
#include "debug.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include "threads.h"  
#include <pthread.h>

// ========== VARIÁVEIS GLOBAIS ==========
static volatile int sigusr1_received = 0;
static session_t* global_sessions = NULL;
static int global_max_games = 0;

// ========== SIGNAL HANDLER ==========
void sigusr1_handler(int sig) {
    (void)sig;
    sigusr1_received = 1;
}

// ========== FUNÇÕES AUXILIARES ==========

// Comparador para qsort (ordem decrescente de pontos)
typedef struct {
    int client_id;
    int points;
} client_score_t;

int compare_scores(const void* a, const void* b) {
    client_score_t* sa = (client_score_t*)a;
    client_score_t* sb = (client_score_t*)b;
    return sb->points - sa->points;  // Ordem decrescente
}

// Gerar ficheiro de log com top 5 clientes
void generate_log_file(session_t* sessions, int max_games) {
    FILE* log_file = fopen("ranking.log", "w");
    if (!log_file) {
        perror("Failed to create ranking.log");
        return;
    }
    
    // Coletar pontuações de todas as sessões ativas
    client_score_t* scores = malloc(max_games * sizeof(client_score_t));
    int n_scores = 0;
    
    for (int i = 0; i < max_games; i++) {
        if (sessions[i].active && sessions[i].board) {
            board_t* board = (board_t*)sessions[i].board;
            scores[n_scores].client_id = sessions[i].client_id;
            scores[n_scores].points = board->pacmans[0].points;
            n_scores++;
        }
    }
    
    // Ordenar por pontuação (decrescente)
    qsort(scores, n_scores, sizeof(client_score_t), compare_scores);
    
    // Escrever top 5
    fprintf(log_file, "=== Top 5 Clients ===\n");
    int limit = (n_scores < 5) ? n_scores : 5;
    for (int i = 0; i < limit; i++) {
        fprintf(log_file, "%d. Client %d: %d points\n", 
                i + 1, scores[i].client_id, scores[i].points);
    }
    
    fclose(log_file);
    free(scores);
    
    debug("Generated ranking.log with %d clients\n", n_scores);
}

// Extrair client_id do req_pipe_path (formato: /tmp/{ID}_request)
int extract_client_id(const char* req_pipe_path) {
    const char* last_slash = strrchr(req_pipe_path, '/');
    if (!last_slash) return -1;
    
    const char* id_start = last_slash + 1;
    char* underscore = strchr(id_start, '_');
    if (!underscore) return -1;
    
    char id_str[16];
    int len = underscore - id_start;
    if (len >= 16) return -1;
    
    strncpy(id_str, id_start, len);
    id_str[len] = '\0';
    
    return atoi(id_str);
}

// ========== THREADS DO JOGO (POR SESSÃO) ==========

// Thread do Pacman - lê comandos do pipe de pedidos
void* pacman_thread_func(void* arg) {
    pacman_thread_args_t* args = (pacman_thread_args_t*)arg;
    board_t* board = (board_t*)args->board;
    game_sync_t* sync = args->sync;
    session_t* session = (session_t*)((char*)sync - offsetof(session_t, sync));
    
    while (sync->game_running && !sync->level_complete && !sync->pacman_dead) {
        // Ler comando do pipe
        char msg[2];
        ssize_t n = read(session->req_pipe_fd, msg, 2);
        
        if (n <= 0) {
            // Cliente desconectou
            pthread_mutex_lock(&sync->board_mutex);
            sync->game_running = 0;
            pthread_cond_broadcast(&sync->display_ready_cond);
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        if (n != 2 || msg[0] != OP_CODE_PLAY) {
            continue;
        }
        
        char command = msg[1];
        
        // Processar comando DISCONNECT
        if (msg[0] == OP_CODE_DISCONNECT) {
            pthread_mutex_lock(&sync->board_mutex);
            sync->game_running = 0;
            pthread_cond_broadcast(&sync->display_ready_cond);
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        // Mover pacman
        pthread_mutex_lock(&sync->board_mutex);
        
        command_t cmd;
        cmd.command = command;
        cmd.turns = 1;
        
        int result = move_pacman(board, 0, &cmd);
        
        if (result == REACHED_PORTAL) {
            sync->level_complete = 1;
            sync->game_running = 0;
        } else if (result == DEAD_PACMAN) {
            sync->pacman_dead = 1;
            sync->game_running = 0;
        }
        
        sync->display_ready = 1;
        pthread_cond_signal(&sync->display_ready_cond);
        pthread_mutex_unlock(&sync->board_mutex);
    }
    
    free(args);
    return NULL;
}

// Thread de um Ghost
void* ghost_thread_func(void* arg) {
    ghost_thread_args_t* args = (ghost_thread_args_t*)arg;
    board_t* board = (board_t*)args->board;
    int ghost_index = args->entity_index;
    game_sync_t* sync = args->sync;
    
    while (sync->game_running && !sync->level_complete && !sync->pacman_dead) {
        ghost_t* ghost = &board->ghosts[ghost_index];
        
        if (ghost->waiting > 0) {
            ghost->waiting--;
            if (board->tempo > 0) {
                usleep((useconds_t)(board->tempo * 1000));
            }
            continue;
        }
        
        command_t* play = &ghost->moves[ghost->current_move % ghost->n_moves];
        
        pthread_mutex_lock(&sync->board_mutex);
        
        int result;
        if (ghost->charged) {
            result = move_ghost_charged(board, ghost_index, play->command);
        } else {
            result = move_ghost(board, ghost_index, play);
        }
        
        if (result == DEAD_PACMAN) {
            sync->pacman_dead = 1;
            sync->game_running = 0;
        }
        
        sync->display_ready = 1;
        pthread_cond_signal(&sync->display_ready_cond);
        pthread_mutex_unlock(&sync->board_mutex);
        
        if (board->tempo > 0) {
            usleep((useconds_t)(board->tempo * 1000));
        } else {
            usleep(50000);
        }
    }
    
    free(args);
    return NULL;
}

// Thread de atualização do board - envia periodicamente o estado ao cliente
void* board_update_thread_func(void* arg) {
    session_t* session = (session_t*)arg;
    board_t* board = (board_t*)session->board;
    game_sync_t* sync = &session->sync;
    
    while (sync->game_running) {
        pthread_mutex_lock(&sync->board_mutex);
        
        while (!sync->display_ready && sync->game_running) {
            pthread_cond_wait(&sync->display_ready_cond, &sync->board_mutex);
        }
        
        if (!sync->game_running) {
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        // Serializar board
        int msg_size = 1 + 6*4 + (board->width * board->height);
        char* msg = malloc(msg_size);
        
        msg[0] = OP_CODE_BOARD;
        memcpy(msg + 1, &board->width, 4);
        memcpy(msg + 5, &board->height, 4);
        memcpy(msg + 9, &board->tempo, 4);
        
        int victory = sync->level_complete ? 1 : 0;
        int game_over = sync->pacman_dead ? 1 : 0;
        int points = board->pacmans[0].points;
        
        memcpy(msg + 13, &victory, 4);
        memcpy(msg + 17, &game_over, 4);
        memcpy(msg + 21, &points, 4);
        
        // Converter board interno para formato de protocolo
        char* board_data = msg + 25;
        for (int y = 0; y < board->height; y++) {
            for (int x = 0; x < board->width; x++) {
                int idx = y * board->width + x;
                board_pos_t* pos = &board->board[idx];
                
                char c = ' ';
                if (pos->content == 'W') c = '#';      // Wall
                else if (pos->content == 'P') c = 'C'; // Pacman (Client)
                else if (pos->content == 'M') c = 'M'; // Ghost/Monster
                else if (pos->has_portal) c = '@';     // Portal
                else if (pos->has_dot) c = '.';        // Dot
                else c = ' ';                          // Empty
                
                board_data[idx] = c;
            }
        }
        
        sync->display_ready = 0;
        pthread_mutex_unlock(&sync->board_mutex);
        
        // Enviar mensagem ao cliente
        write(session->notif_pipe_fd, msg, msg_size);
        free(msg);
        
        // Aguardar próximo ciclo
        if (board->tempo > 0) {
            usleep((useconds_t)(board->tempo * 1000));
        } else {
            usleep(50000);
        }
    }
    
    // Enviar mensagem final (game over ou victory)
    pthread_mutex_lock(&sync->board_mutex);
    
    int msg_size = 1 + 6*4 + (board->width * board->height);
    char* msg = malloc(msg_size);
    
    msg[0] = OP_CODE_BOARD;
    memcpy(msg + 1, &board->width, 4);
    memcpy(msg + 5, &board->height, 4);
    memcpy(msg + 9, &board->tempo, 4);
    
    int victory = sync->level_complete ? 1 : 0;
    int game_over = sync->pacman_dead ? 1 : 0;
    int points = board->pacmans[0].points;
    
    memcpy(msg + 13, &victory, 4);
    memcpy(msg + 17, &game_over, 4);
    memcpy(msg + 21, &points, 4);
    
    char* board_data = msg + 25;
    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            board_pos_t* pos = &board->board[idx];
            
            char c = ' ';
            if (pos->content == 'W') c = '#';
            else if (pos->content == 'P') c = 'C';
            else if (pos->content == 'M') c = 'M';
            else if (pos->has_portal) c = '@';
            else if (pos->has_dot) c = '.';
            else c = ' ';
            
            board_data[idx] = c;
        }
    }
    
    pthread_mutex_unlock(&sync->board_mutex);
    
    write(session->notif_pipe_fd, msg, msg_size);
    free(msg);
    
    return NULL;
}

// ========== THREAD GESTORA DE SESSÃO ==========

void* session_manager_thread_func(void* arg) {
    session_manager_args_t* args = (session_manager_args_t*)arg;
    int session_index = args->session_index;
    session_t* sessions = args->sessions;
    connection_buffer_t* buffer = args->buffer;
    char* levels_directory = args->levels_directory;
    
    // Bloquear SIGUSR1 nesta thread
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    
    while (1) {
        // Esperar por novo pedido (consumidor)
        sem_wait(&buffer->full);
        
        pthread_mutex_lock(&buffer->mutex);
        
        // Extrair pedido do buffer
        connection_request_t request = buffer->requests[buffer->tail];
        buffer->tail = (buffer->tail + 1) % buffer->max_size;
        buffer->count--;
        
        pthread_mutex_unlock(&buffer->mutex);
        sem_post(&buffer->empty);
        
        // Processar pedido
        session_t* session = &sessions[session_index];
        session->active = 1;
        session->client_id = extract_client_id(request.req_pipe_path);
        
        strcpy(session->req_pipe_path, request.req_pipe_path);
        strcpy(session->notif_pipe_path, request.notif_pipe_path);
        
        debug("Session %d: Processing connection from client %d\n", 
              session_index, session->client_id);
        
        // Abrir pipes do cliente
        session->req_pipe_fd = open(request.req_pipe_path, O_RDONLY);
        if (session->req_pipe_fd < 0) {
            perror("Failed to open request pipe");
            session->active = 0;
            continue;
        }
        
        session->notif_pipe_fd = open(request.notif_pipe_path, O_WRONLY);
        if (session->notif_pipe_fd < 0) {
            perror("Failed to open notification pipe");
            close(session->req_pipe_fd);
            session->active = 0;
            continue;
        }
        
        // Enviar resposta CONNECT
        char response[2];
        response[0] = OP_CODE_CONNECT;
        response[1] = 0;  // Success
        write(session->notif_pipe_fd, response, 2);
        
        debug("Session %d: Sent CONNECT response\n", session_index);
        
        // Carregar nível
        char** level_files = NULL;
        int n_levels = 0;
        
        DIR* dir = opendir(levels_directory);
        if (!dir) {
            perror("Failed to open levels directory");
            close(session->req_pipe_fd);
            close(session->notif_pipe_fd);
            session->active = 0;
            continue;
        }
        
        struct dirent* entry;
        level_files = malloc(MAX_LEVELS * sizeof(char*));
        
        while ((entry = readdir(dir)) != NULL && n_levels < MAX_LEVELS) {
            if (strstr(entry->d_name, ".lvl")) {
                level_files[n_levels] = malloc(512);
                strncpy(level_files[n_levels], entry->d_name, 511);
                level_files[n_levels][511] = '\0';
                n_levels++;
            }
        }
        closedir(dir);
        
        if (n_levels == 0) {
            debug("Session %d: No levels found\n", session_index);
            close(session->req_pipe_fd);
            close(session->notif_pipe_fd);
            session->active = 0;
            for (int i = 0; i < n_levels; i++) free(level_files[i]);
            free(level_files);
            continue;
        }
        
        // Criar board
        board_t* board = malloc(sizeof(board_t));
        session->board = board;
        
        // Remover extensão .lvl do nome
        char level_name[512];
        strncpy(level_name, level_files[0], 511);
        level_name[511] = '\0';
        char* ext = strstr(level_name, ".lvl");
        if (ext) *ext = '\0';
        
        level_data_t level_data;
        if (parse_level_file(levels_directory, level_name, &level_data) != 0) {
            debug("Session %d: Failed to parse level %s\n", session_index, level_name);
            close(session->req_pipe_fd);
            close(session->notif_pipe_fd);
            session->active = 0;
            free(board);
            for (int i = 0; i < n_levels; i++) free(level_files[i]);
            free(level_files);
            continue;
        }
        
        load_level(board, 0, &level_data, levels_directory);
        
        // Inicializar sincronização
        init_game_sync(&session->sync);
        session->sync.game_running = 1;
        session->sync.display_ready = 1;
        
        // Criar threads do jogo
        // Board update thread
        pthread_create(&session->board_update_thread, NULL, board_update_thread_func, session);
        
        // Pacman thread
        pacman_thread_args_t* pacman_args = malloc(sizeof(pacman_thread_args_t));
        pacman_args->board = board;
        pacman_args->sync = &session->sync;
        pthread_create(&session->pacman_thread, NULL, pacman_thread_func, pacman_args);
        
        // Ghost threads
        session->n_ghost_threads = board->n_ghosts;
        session->ghost_threads = malloc(session->n_ghost_threads * sizeof(pthread_t));
        
        for (int i = 0; i < session->n_ghost_threads; i++) {
            ghost_thread_args_t* ghost_args = malloc(sizeof(ghost_thread_args_t));
            ghost_args->board = board;
            ghost_args->entity_index = i;
            ghost_args->sync = &session->sync;
            
            // Bloquear SIGUSR1 nas threads de ghost
            pthread_create(&session->ghost_threads[i], NULL, ghost_thread_func, ghost_args);
        }
        
        debug("Session %d: Game threads created\n", session_index);
        
        // Esperar fim do jogo
        pthread_join(session->pacman_thread, NULL);
        
        pthread_mutex_lock(&session->sync.board_mutex);
        session->sync.game_running = 0;
        pthread_cond_broadcast(&session->sync.display_ready_cond);
        pthread_mutex_unlock(&session->sync.board_mutex);
        
        for (int i = 0; i < session->n_ghost_threads; i++) {
            pthread_join(session->ghost_threads[i], NULL);
        }
        
        pthread_join(session->board_update_thread, NULL);
        
        debug("Session %d: Game ended (victory=%d, dead=%d)\n", 
              session_index, session->sync.level_complete, session->sync.pacman_dead);
        
        // Limpar recursos
        close(session->req_pipe_fd);
        close(session->notif_pipe_fd);
        unload_level(board);
        free(board);
        free(session->ghost_threads);
        destroy_game_sync(&session->sync);
        
        for (int i = 0; i < n_levels; i++) free(level_files[i]);
        free(level_files);
        
        session->active = 0;
        session->board = NULL;
        
        debug("Session %d: Resources cleaned up\n", session_index);
    }
    
    free(args);
    return NULL;
}

// ========== THREAD ANFITRIÃ (HOST) ==========

void* host_thread_func(void* arg) {
    host_thread_args_t* args = (host_thread_args_t*)arg;
    int register_pipe_fd = args->register_pipe_fd;
    connection_buffer_t* buffer = args->buffer;
    
    debug("Host thread started, waiting for connections...\n");
    
    while (1) {
        // Ler mensagem CONNECT (1 + 40 + 40 = 81 bytes)
        char msg[81];
        ssize_t n = read(register_pipe_fd, msg, 81);
        
        if (n <= 0) {
            if (n == 0) {
                debug("Host thread: Register pipe closed\n");
            } else {
                perror("Host thread: read failed");
            }
            break;
        }
        
        if (n != 81 || msg[0] != OP_CODE_CONNECT) {
            debug("Host thread: Invalid CONNECT message (size=%zd, opcode=%d)\n", n, msg[0]);
            continue;
        }
        
        // Extrair paths dos pipes
        connection_request_t request;
        memcpy(request.req_pipe_path, msg + 1, MAX_PIPE_PATH_LENGTH);
        request.req_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
        memcpy(request.notif_pipe_path, msg + 41, MAX_PIPE_PATH_LENGTH);
        request.notif_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
        
        debug("Host thread: Received CONNECT from %s\n", request.req_pipe_path);
        
        // Verificar flag SIGUSR1
        if (sigusr1_received) {
            debug("Host thread: Generating log file...\n");
            generate_log_file(global_sessions, global_max_games);
            sigusr1_received = 0;
        }
        
        // Inserir no buffer (produtor)
        sem_wait(&buffer->empty);  // Bloqueia se buffer cheio
        
        pthread_mutex_lock(&buffer->mutex);
        
        buffer->requests[buffer->head] = request;
        buffer->head = (buffer->head + 1) % buffer->max_size;
        buffer->count++;
        
        pthread_mutex_unlock(&buffer->mutex);
        sem_post(&buffer->full);
        
        debug("Host thread: Request queued (buffer count=%d)\n", buffer->count);
    }
    
    free(args);
    return NULL;
}

// ========== MAIN ==========

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <levels_dir> <max_games> <register_fifo>\n", argv[0]);
        return 1;
    }
    
    char* levels_directory = argv[1];
    int max_games = atoi(argv[2]);
    char* register_fifo_path = argv[3];
    
    if (max_games <= 0) {
        fprintf(stderr, "Error: max_games must be positive\n");
        return 1;
    }
    
    open_debug_file("debug.log");
    debug("Server starting: max_games=%d, register_pipe=%s\n", max_games, register_fifo_path);
    
    // Registar signal handler para SIGUSR1
    signal(SIGUSR1, sigusr1_handler);
    
    // Criar named pipe de registo
    unlink(register_fifo_path);
    if (mkfifo(register_fifo_path, 0666) != 0) {
        perror("Failed to create register FIFO");
        return 1;
    }
    
    // Abrir em modo RDWR para evitar bloqueio até primeiro cliente conectar
    int register_pipe_fd = open(register_fifo_path, O_RDWR);
    if (register_pipe_fd < 0) {
        perror("Failed to open register FIFO");
        unlink(register_fifo_path);
        return 1;
    }
    
    debug("Register pipe created and opened\n");
    
    // Inicializar buffer produtor-consumidor
    connection_buffer_t buffer;
    buffer.requests = malloc(max_games * sizeof(connection_request_t));
    buffer.head = 0;
    buffer.tail = 0;
    buffer.count = 0;
    buffer.max_size = max_games;
    pthread_mutex_init(&buffer.mutex, NULL);
    sem_init(&buffer.empty, 0, max_games);  // Inicialmente todos vazios
    sem_init(&buffer.full, 0, 0);           // Inicialmente nenhum cheio
    
    // Inicializar array de sessões
    session_t* sessions = calloc(max_games, sizeof(session_t));
    global_sessions = sessions;
    global_max_games = max_games;
    
    // Criar threads gestoras
    pthread_t* session_manager_threads = malloc(max_games * sizeof(pthread_t));
    
    for (int i = 0; i < max_games; i++) {
        session_manager_args_t* args = malloc(sizeof(session_manager_args_t));
        args->session_index = i;
        args->sessions = sessions;
        args->buffer = &buffer;
        args->levels_directory = levels_directory;
        
        pthread_create(&session_manager_threads[i], NULL, session_manager_thread_func, args);
    }
    
    debug("Created %d session manager threads\n", max_games);
    
    // Criar thread anfitriã
    pthread_t host_thread;
    host_thread_args_t* host_args = malloc(sizeof(host_thread_args_t));
    host_args->register_pipe_fd = register_pipe_fd;
    host_args->buffer = &buffer;
    
    pthread_create(&host_thread, NULL, host_thread_func, host_args);
    
    debug("Host thread created, server ready\n");
    
    // Esperar thread anfitriã (nunca termina normalmente)
    pthread_join(host_thread, NULL);
    
    // Cleanup (nunca alcançado em operação normal)
    for (int i = 0; i < max_games; i++) {
        pthread_cancel(session_manager_threads[i]);
        pthread_join(session_manager_threads[i], NULL);
    }
    
    free(session_manager_threads);
    free(sessions);
    free(buffer.requests);
    pthread_mutex_destroy(&buffer.mutex);
    sem_destroy(&buffer.empty);
    sem_destroy(&buffer.full);
    
    close(register_pipe_fd);
    unlink(register_fifo_path);
    close_debug_file();
    
    return 0;
}
