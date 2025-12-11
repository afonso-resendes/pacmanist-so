#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include "board.h"

// Estrutura para sincronização entre threads
typedef struct {
    pthread_mutex_t board_mutex;
    pthread_cond_t game_tick_cond;
    pthread_cond_t display_ready_cond;
    int game_running;
    int display_ready;
} game_sync_t;



/* Inicializa a estrutura de sincronização */
int init_game_sync(game_sync_t* sync);

/* Destrói a estrutura de sincronização */
void destroy_game_sync(game_sync_t* sync);

#endif

