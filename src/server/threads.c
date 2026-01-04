#include "threads.h"
#include <stdlib.h>

/* Inicializa a estrutura de sincronização */
int init_game_sync(game_sync_t* sync) {
    // Inicializar mutex
    if (pthread_mutex_init(&sync->board_mutex, NULL) != 0) {
        return -1;
    }
    
    // Inicializar condition variables
    if (pthread_cond_init(&sync->display_ready_cond, NULL) != 0) {
        pthread_mutex_destroy(&sync->board_mutex);
        return -1;
    }
    
    if (pthread_cond_init(&sync->game_tick_cond, NULL) != 0) {
        pthread_mutex_destroy(&sync->board_mutex);
        pthread_cond_destroy(&sync->display_ready_cond);
        return -1;
    }
    
    // Inicializar flags
    sync->game_running = 1;
    sync->display_ready = 0;
    sync->level_complete = 0;
    sync->pacman_dead = 0;
    sync->quick_save_requested = 0;
    
    return 0;
}

/* Destrói a estrutura de sincronização */
void destroy_game_sync(game_sync_t* sync) {
    pthread_mutex_destroy(&sync->board_mutex);
    pthread_cond_destroy(&sync->display_ready_cond);
    pthread_cond_destroy(&sync->game_tick_cond);
}

