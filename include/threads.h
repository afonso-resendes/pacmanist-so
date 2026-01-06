#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <sys/types.h>
#include <semaphore.h>

#define MAX_PIPE_PATH_LENGTH 40

typedef struct {
    pthread_mutex_t board_mutex;           // Protege acesso ao board_t
    pthread_cond_t display_ready_cond;     // Sinaliza display thread
    pthread_cond_t game_tick_cond;         // Sinaliza fim do desenho
    
    volatile int game_running;             // 1 = jogo ativo, 0 = terminar
    volatile int display_ready;            // 1 = precisa redesenhar
    volatile int level_complete;           // 1 = portal alcançado
    volatile int pacman_dead;              // 1 = pacman morreu
    volatile int quick_save_requested;      // NOVO: flag para G key
} game_sync_t;

// Estruturas para argumentos das threads
typedef struct {
    void* board;                           // board_t*
    int entity_index;                      // Índice do ghost (0 = primeiro)
    game_sync_t* sync;                     // Ponteiro para sincronização
} ghost_thread_args_t;

typedef struct {
    void* board;                           // board_t*
    game_sync_t* sync;                     // Ponteiro para sincronização
} pacman_thread_args_t;

// ========== ESTRUTURAS PARA SERVIDOR MULTI-SESSÃO ==========

// Estrutura para pedidos de conexão (buffer produtor-consumidor)
typedef struct {
    char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
} connection_request_t;

// Buffer circular produtor-consumidor
typedef struct {
    connection_request_t* requests;    // Array circular de pedidos
    int head;                          // Índice de inserção (produtor)
    int tail;                          // Índice de extração (consumidor)
    int count;                         // Número de pedidos no buffer
    int max_size;                      // Tamanho máximo (max_games)
    pthread_mutex_t mutex;             // Protege acesso ao buffer
    sem_t empty;                       // Semáforo: slots vazios
    sem_t full;                        // Semáforo: slots ocupados
} connection_buffer_t;

// Estrutura de sessão (uma por cliente)
typedef struct {
    int client_id;                     // ID único do cliente
    int active;                        // 1 = sessão ativa, 0 = terminada
    int req_pipe_fd;                   // File descriptor do pipe de pedidos
    int notif_pipe_fd;                 // File descriptor do pipe de notificações
    char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
    void* board;                       // board_t* (ponteiro para tabuleiro da sessão)
    pthread_t pacman_thread;           // Thread do pacman desta sessão
    pthread_t* ghost_threads;          // Array de threads dos ghosts
    int n_ghost_threads;               // Número de threads de ghosts
    pthread_t board_update_thread;     // Thread que envia updates periódicos
    game_sync_t sync;                  // Sincronização específica desta sessão
} session_t;

// Argumentos para thread gestora de sessão
typedef struct {
    int session_index;                 // Índice no array de sessões
    session_t* sessions;               // Ponteiro para array de sessões
    connection_buffer_t* buffer;       // Ponteiro para buffer produtor-consumidor
    char* levels_directory;            // Diretório dos níveis
} session_manager_args_t;

// Argumentos para thread anfitriã (host)
typedef struct {
    int register_pipe_fd;              // FD do pipe de registo
    connection_buffer_t* buffer;       // Ponteiro para buffer produtor-consumidor
} host_thread_args_t;

// Funções de inicialização
int init_game_sync(game_sync_t* sync);
void destroy_game_sync(game_sync_t* sync);

#endif

