#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

// Variável global para guardar o PID do processo que está a jogar
static pid_t playing_pid = -1;
// Flag para indicar se este processo é o backup (pai original)
static int is_backup = 0;

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    
    command_t* play;
    if (pacman->n_moves == 0) {
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        // Quick save com tecla 'G' - apenas se for o processo que está a jogar
        if(c.command == 'G' && !is_backup) {
            return CREATE_BACKUP;
        }

        c.turns = 1;
        play = &c;
    }
    else {
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    char* level_directory = argv[1];
    
    srand((unsigned int)time(NULL));
    open_debug_file("debug.log");
    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;

    int current_level = 1;
    char level_name[32];

    level_data_t level_data;
     snprintf(level_name, sizeof(level_name), "%d", current_level);
    if (parse_level_file(level_directory, level_name, &level_data) != 0) {  // Mudar "1" para "3"
        printf("ERRO: Não consegui carregar o nível!\n");
        return 1;
    }

    while (!end_game) {
        load_level(&game_board, accumulated_points, &level_data, level_directory);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            // Handler para criar backup - LÓGICA INVERTIDA
            if(result == CREATE_BACKUP && !is_backup) {
                debug("CURRENT: G key pressed, creating backup with fork()...\n");
                
                // Verificar se já existe um backup (processo pai suspenso)
                if(playing_pid != -1) {
                    debug("CURRENT: Backup already exists, ignoring G key\n");
                    continue;
                }

                // Criar processo filho que vai CONTINUAR A JOGAR
                playing_pid = fork();
                
                if(playing_pid < 0) {
                    debug("ERROR: Fork failed\n");
                    continue;
                }
                else if(playing_pid == 0) {
                    // PROCESSO FILHO - Continua a jogar
                    playing_pid = -1; // No filho, não há playing_pid
                    is_backup = 0;    // Filho não é backup
                    
                    debug("CHILD: Created with PID %d, continuing to play...\n", getpid());
                    
                    // Filho continua o loop normalmente
                    continue;
                }
                else {
                    // PROCESSO PAI - Torna-se o backup e suspende-se
                    is_backup = 1;
                    
                    debug("PARENT: Becoming backup (PID %d), child playing (PID %d)\n", getpid(), playing_pid);
                    debug("PARENT: Suspending with SIGSTOP...\n");
                    fflush(NULL);
                    
                    // PAI suspende-se (guarda o estado)
                    raise(SIGSTOP);
                    
                    // Quando o pai acorda (filho morreu), continua daqui
                    debug("PARENT: ====== BACKUP ACORDOU! ====== PID %d\n", getpid());
                    debug("PARENT: Child died, resuming from saved position...\n");
                    
                    // Ressuscitar o Pacman
                    game_board.pacmans[0].alive = true;
                    playing_pid = -1; // Não há mais processo filho
                    is_backup = 0;    // Voltamos a ser o processo principal
                    
                    // Redesenhar
                    clear();
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();
                    
                    debug("PARENT: Resumed gameplay from backup state\n");
                    
                    // Continuar o jogo
                    continue;
                }
            }

            if(result == NEXT_LEVEL) {
                // Se somos o filho e o pai está suspenso, matar o pai
                if(playing_pid == -1 && is_backup == 0) {
                    // Procurar processo pai
                    pid_t parent = getppid();
                    debug("CHILD: Next level, killing backup parent %d\n", parent);
                    kill(parent, SIGKILL);
                }
                
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                break;
            }

            if(result == QUIT_GAME) {
                // Se somos o filho e morreu
                if(playing_pid == -1 && is_backup == 0) {
                    pid_t parent = getppid();
                    
                    // Verificar se o pai ainda existe (é o backup)
                    if(kill(parent, 0) == 0) {
                        debug("CHILD: Died, waking backup parent %d\n", parent);
                        
                        // Acordar o pai
                        kill(parent, SIGCONT);
                        
                        // Filho termina
                        unload_level(&game_board);
                        terminal_cleanup();
                        close_debug_file();
                        exit(0);
                    }
                }
                
                // Se não há backup, game over
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 
            accumulated_points = game_board.pacmans[0].points;      
        }
        
        print_board(&game_board);
        unload_level(&game_board);
    }    

    terminal_cleanup();
    close_debug_file();

    return 0;
}
