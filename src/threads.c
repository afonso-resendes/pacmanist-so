#include "threads.h"
#include <stdlib.h>

/* Inicializa a estrutura de sincronização */
int init_game_sync(game_sync_t* sync) {
    if (sync == NULL) {
        return -1;
    }

    // Inicializar mutex
    if (pthread_mutex_init(&sync->board_mutex, NULL) != 0) {
        return -1;
    }

    // Inicializar condition variables
    if (pthread_cond_init(&sync->game_tick_cond, NULL) != 0) {
        pthread_mutex_destroy(&sync->board_mutex);
        return -1;
    }

    if (pthread_cond_init(&sync->display_ready_cond, NULL) != 0) {
        pthread_cond_destroy(&sync->game_tick_cond);
        pthread_mutex_destroy(&sync->board_mutex);
        return -1;
    }

    // Inicializar flags
    sync->game_running = 1;
    sync->display_ready = 0;

    return 0;
}

/* Destrói a estrutura de sincronização */
void destroy_game_sync(game_sync_t* sync) {
    if (sync == NULL) {
        return;
    }

    pthread_cond_destroy(&sync->display_ready_cond);
    pthread_cond_destroy(&sync->game_tick_cond);
    pthread_mutex_destroy(&sync->board_mutex);
}

