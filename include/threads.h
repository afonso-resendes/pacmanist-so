#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <sys/types.h>

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

// Funções de inicialização
int init_game_sync(game_sync_t* sync);
void destroy_game_sync(game_sync_t* sync);

#endif

