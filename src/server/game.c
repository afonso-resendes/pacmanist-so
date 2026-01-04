#include "board.h"
#include "server_display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include "threads.h"  
#include <pthread.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

// Sincronização global
static game_sync_t game_sync;

// PID do processo backup (para quick save)
static pid_t backup_parent_pid = -1;

// Arrays de threads
static pthread_t display_thread;
static pthread_t pacman_thread;
static pthread_t* ghost_threads = NULL;
static int n_ghost_threads = 0;

// ========== FORWARD DECLARATIONS ==========
void* display_thread_func(void* arg);
void* pacman_thread_func(void* arg);
void* ghost_thread_func(void* arg);

// ========== FUNÇÕES AUXILIARES PARA AS THREADS ==========

// Parar todas as threads de forma ordenada
void stop_all_threads(void) {
    
    // Sinalizar para as threads pararem
    pthread_mutex_lock(&game_sync.board_mutex);
    game_sync.game_running = 0;
    pthread_cond_broadcast(&game_sync.display_ready_cond);
    pthread_mutex_unlock(&game_sync.board_mutex);
    
    // Esperar threads
    pthread_join(pacman_thread, NULL);
    
    for (int i = 0; i < n_ghost_threads; i++) {
        pthread_join(ghost_threads[i], NULL);
    }
    
    pthread_join(display_thread, NULL);
    
    if (ghost_threads) {
        free(ghost_threads);
        ghost_threads = NULL;
    }
}

// Criar todas as threads
int create_all_threads(board_t* game_board) {
    
    // Reset flags
    game_sync.game_running = 1;
    game_sync.display_ready = 1;
    game_sync.level_complete = 0;
    game_sync.pacman_dead = 0;
    game_sync.quick_save_requested = 0;
    
    // Display thread
    if (pthread_create(&display_thread, NULL, display_thread_func, game_board) != 0) {
        return -1;
    }
    
    // Pacman thread
    pacman_thread_args_t* pacman_args = malloc(sizeof(pacman_thread_args_t));
    pacman_args->board = game_board;
    pacman_args->sync = &game_sync;
    
    if (pthread_create(&pacman_thread, NULL, pacman_thread_func, pacman_args) != 0) {
        free(pacman_args);
        return -1;
    }
    
    // Ghost threads
    n_ghost_threads = game_board->n_ghosts;
    ghost_threads = malloc(n_ghost_threads * sizeof(pthread_t));
    
    for (int i = 0; i < n_ghost_threads; i++) {
        ghost_thread_args_t* ghost_args = malloc(sizeof(ghost_thread_args_t));
        ghost_args->board = game_board;
        ghost_args->entity_index = i;
        ghost_args->sync = &game_sync;
        
        if (pthread_create(&ghost_threads[i], NULL, ghost_thread_func, ghost_args) != 0) {
            free(ghost_args);
            return -1;
        }
    }
    
    return 0;
}

// ========== THREAD DO DISPLAY ==========

void* display_thread_func(void* arg) {
    board_t* game_board = (board_t*)arg;
    
    
    while (game_sync.game_running) {
        pthread_mutex_lock(&game_sync.board_mutex);
        
        while (!game_sync.display_ready && game_sync.game_running) {
            pthread_cond_wait(&game_sync.display_ready_cond, &game_sync.board_mutex);
        }
        
        if (!game_sync.game_running) {
            pthread_mutex_unlock(&game_sync.board_mutex);
            break;
        }
        
        draw_board(game_board, DRAW_MENU);
        refresh_screen();
        game_sync.display_ready = 0;
        
        // Avisar que terminou desenho
        pthread_cond_broadcast(&game_sync.game_tick_cond);
        
        pthread_mutex_unlock(&game_sync.board_mutex);
        
        // Frame rate control
        if (game_board->tempo > 0) {
            sleep_ms(game_board->tempo);
        } else {
            sleep_ms(50); // 20 FPS default
        }
    }
    
    return NULL;
}

// ========== THREAD DO PACMAN ==========

void* pacman_thread_func(void* arg) {
    pacman_thread_args_t* args = (pacman_thread_args_t*)arg;
    board_t* board = (board_t*)args->board;
    game_sync_t* sync = args->sync;
    
    
    while (sync->game_running && !sync->level_complete && !sync->pacman_dead) {
        pacman_t* pacman = &board->pacmans[0];
        command_t* play;
        command_t c;
        
        if (pacman->n_moves == 0) {
            c.command = get_input();
            
            if (c.command == '\0') {
                continue;
            }
            
            
            // QUICK SAVE - sinalizar main thread via flag
            if (c.command == 'G') {
                pthread_mutex_lock(&sync->board_mutex);
                sync->quick_save_requested = 1;
                sync->game_running = 0;
                pthread_cond_broadcast(&sync->display_ready_cond);
                pthread_mutex_unlock(&sync->board_mutex);
                break;
            }
            
            // QUIT - terminar completamente (matar backup se existir)
            if (c.command == 'Q') {
                pthread_mutex_lock(&sync->board_mutex);
                sync->game_running = 0;
                sync->display_ready = 1;
                pthread_cond_broadcast(&sync->display_ready_cond);
                pthread_mutex_unlock(&sync->board_mutex);
                break;
            }
            
            c.turns = 1;
            play = &c;
        } else {
            play = &pacman->moves[pacman->current_move % pacman->n_moves];
        }
        
        pthread_mutex_lock(&sync->board_mutex);
        
        int result = move_pacman(board, 0, play);
        
        if (result == REACHED_PORTAL) {
            sync->level_complete = 1;
            sync->game_running = 0;
            sync->display_ready = 1;
            pthread_cond_broadcast(&sync->display_ready_cond);
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        if (result == DEAD_PACMAN || !pacman->alive) {
            sync->pacman_dead = 1;
            sync->game_running = 0;
            sync->display_ready = 1;
            pthread_cond_broadcast(&sync->display_ready_cond);
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        sync->display_ready = 1;
        pthread_cond_signal(&sync->display_ready_cond);
        
        pthread_mutex_unlock(&sync->board_mutex);
        
        if (board->tempo > 0) {
            sleep_ms(board->tempo);
        }
    }
    
    free(args);
    return NULL;
}

// ========== THREAD DOS GHOSTS (uma thread por ghost) ==========

void* ghost_thread_func(void* arg) {
    ghost_thread_args_t* args = (ghost_thread_args_t*)arg;
    board_t* board = (board_t*)args->board;
    int ghost_index = args->entity_index;
    game_sync_t* sync = args->sync;
    
    
    while (sync->game_running && !sync->level_complete && !sync->pacman_dead) {
        ghost_t* ghost = &board->ghosts[ghost_index];
        
        // Obter próximo movimento
        command_t* move = &ghost->moves[ghost->current_move % ghost->n_moves];
        
        pthread_mutex_lock(&sync->board_mutex);
        
        if (!sync->game_running) {
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        move_ghost(board, ghost_index, move);
        
        if (!board->pacmans[0].alive) {
            sync->pacman_dead = 1;
            sync->game_running = 0;
            sync->display_ready = 1;
            pthread_cond_broadcast(&sync->display_ready_cond);
            pthread_mutex_unlock(&sync->board_mutex);
            break;
        }
        
        // Sinalizar display
        sync->display_ready = 1;
        pthread_cond_signal(&sync->display_ready_cond);
        
        pthread_mutex_unlock(&sync->board_mutex);
        
        // Delay baseado no tempo do jogo
        if (board->tempo > 0) {
            sleep_ms(board->tempo);
        } else {
            sleep_ms(100);
        }
    }
    
    free(args);
    return NULL;
}

// ========== FUNÇÕES AUXILIARES ==========

void screen_refresh(board_t * game_board, int mode) {
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

static int compare_level_names(const void* a, const void* b) {
    const char* name1 = (const char*)a;
    const char* name2 = (const char*)b;
    
    bool is_num1 = true;
    bool is_num2 = true;
    
    for (int i = 0; name1[i] != '\0'; i++) {
        if (!isdigit(name1[i])) {
            is_num1 = false;
            break;
        }
    }
    
    for (int i = 0; name2[i] != '\0'; i++) {
        if (!isdigit(name2[i])) {
            is_num2 = false;
            break;
        }
    }
    
    if (is_num1 && is_num2) {
        int num1 = atoi(name1);
        int num2 = atoi(name2);
        return num1 - num2;
    }
    
    return strcmp(name1, name2);
}

void sort_level_files(char level_names[][MAX_FILENAME], int n_levels) {
    qsort(level_names, n_levels, MAX_FILENAME, compare_level_names);
}

int find_level_files(char* level_directory, char level_names[][MAX_FILENAME]) {
    DIR* dir = opendir(level_directory);
    if (dir == NULL) {
        return -1;
    }
    
    int count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL && count < MAX_LEVELS) {
        char* name = entry->d_name;
        size_t len = strlen(name);
        
        if (len > 4 && strcmp(name + len - 4, ".lvl") == 0) {
            size_t name_len = len - 4;
            if (name_len < MAX_FILENAME) {
                strncpy(level_names[count], name, name_len);
                level_names[count][name_len] = '\0';
                count++;
            }
        }
    }
    
    closedir(dir);
    
    if (count == 0) {
        return 0;
    }
    
    return count;
}

// ========== MAIN FUNC ==========

int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }

    char* level_directory = argv[1];
    
    srand((unsigned int)time(NULL));
    open_debug_file("debug.log");
    terminal_init();

    if (init_game_sync(&game_sync) != 0) {
        terminal_cleanup();
        close_debug_file();
        return 1;
    }
    
    char level_names[MAX_LEVELS][MAX_FILENAME];
    int n_levels = find_level_files(level_directory, level_names);
    
    if (n_levels <= 0) {
        terminal_cleanup();
        close_debug_file();
        return 1;
    }
    
    sort_level_files(level_names, n_levels);
    
   
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;
    int current_level_index = 0;
    
    while (!end_game && current_level_index < n_levels) {
        char level_name[MAX_FILENAME];
        level_data_t level_data;
        
        snprintf(level_name, sizeof(level_name), "%s", level_names[current_level_index]);
        
        if (parse_level_file(level_directory, level_name, &level_data) != 0) {
            break;
        }
        
        load_level(&game_board, accumulated_points, &level_data, level_directory);
        
        // ===== CRIAR THREADS INICIAIS =====
        if (create_all_threads(&game_board) != 0) {
            goto cleanup_level;
        }
        
        // ===== GAME LOOP COM QUICK SAVE =====
        while (true) {
            // Esperar threads terminarem
            pthread_join(pacman_thread, NULL);
            
            for (int i = 0; i < n_ghost_threads; i++) {
                pthread_join(ghost_threads[i], NULL);
            }
            
            pthread_mutex_lock(&game_sync.board_mutex);
            game_sync.game_running = 0;
            pthread_cond_broadcast(&game_sync.display_ready_cond);
            pthread_mutex_unlock(&game_sync.board_mutex);
            
            pthread_join(display_thread, NULL);
            
            free(ghost_threads);
            ghost_threads = NULL;
            
            // ===== QUICK SAVE =====
            if (game_sync.quick_save_requested) {
                
                // Só pode existir 1 backup
                if (backup_parent_pid != -1) {
                    if (kill(backup_parent_pid, 0) == 0) {
                        
                        // Reiniciar threads e continuar
                        if (create_all_threads(&game_board) != 0) {
                            goto cleanup_level;
                        }
                        continue;
                    } else {
                        backup_parent_pid = -1;
                    }
                }
                
                fflush(NULL);
                
                pid_t child_pid = fork();
                
                if (child_pid < 0) {
                    goto cleanup_level;
                }
                else if (child_pid == 0) {
                    // ===== CHILD: continua a jogar =====
                    backup_parent_pid = getppid();
                  
                    
                    // Reiniciar threads
                    if (create_all_threads(&game_board) != 0) {
                        exit(1);
                    }
                    
                    continue; // Continua loop
                }
                else {
                    // ===== PARENT: Torna-se backup =====
                   
                    fflush(NULL);
                    
                    raise(SIGSTOP);
                    
                    // ===== ACORDOU - Child morreu =====
                   
                    
                    // Ressuscitar pacman
                    game_board.pacmans[0].alive = true;
                    backup_parent_pid = -1;
                    
                    // Redesenhar
                    clear();
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();
                 
                    
                    // Reiniciar threads
                    if (create_all_threads(&game_board) != 0) {
                        goto cleanup_level;
                    }
                    
                    continue; // Continua loop
                }
            }
            
            // ===== VERIFICAR OUTROS RESULTADOS =====
            
            if (game_sync.level_complete) {
                // Matar backup se existir
                if (backup_parent_pid != -1) {
                 
                    kill(backup_parent_pid, SIGKILL);
                    waitpid(backup_parent_pid, NULL, 0);
                    backup_parent_pid = -1;
                }
                
                accumulated_points = game_board.pacmans[0].points;
                current_level_index++;
                
                if (current_level_index >= n_levels) {
                   
                    screen_refresh(&game_board, DRAW_WIN);
                    end_game = true;
                } else {
                   
                    screen_refresh(&game_board, DRAW_WIN);
                    clear();
                }
                break;
            }
            
            if (game_sync.pacman_dead) {
                // Acordar backup se existir
                if (backup_parent_pid != -1) {
                  
                    
                    if (kill(backup_parent_pid, 0) == 0) {
                        kill(backup_parent_pid, SIGCONT);
                        
                        // Child termina
                        unload_level(&game_board);
                        close_debug_file();
                        exit(0);
                    } else {
                       
                        backup_parent_pid = -1;
                    }
                }
                
                // Sem backup = game over
             
                screen_refresh(&game_board, DRAW_GAME_OVER);
                end_game = true;
                break;
            }
            
            // ===== QUIT (Q) - TERMINAR COMPLETAMENTE =====
            // Se chegou aqui sem level_complete nem pacman_dead, foi quit
         
            
            // Matar backup se existir (NÃO acordar, MATAR)
            if (backup_parent_pid != -1) {
              
                kill(backup_parent_pid, SIGKILL);
                waitpid(backup_parent_pid, NULL, 0);
                backup_parent_pid = -1;
            }
            

            end_game = true;
            break;
        }
        
cleanup_level:
        print_board(&game_board);
        unload_level(&game_board);
    }
    
    if (current_level_index >= n_levels && !end_game) {
        printf("🎉 Parabéns! Completaste todos os %d níveis!\n", n_levels);
        printf("Pontos finais: %d\n", accumulated_points);
    }

    destroy_game_sync(&game_sync);
    terminal_cleanup();
    close_debug_file();

    return 0;
}
