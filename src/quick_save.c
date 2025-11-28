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

// Variável global para guardar o PID do processo de backup
static pid_t backup_pid = -1;

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
    if (pacman->n_moves == 0) { // if is user input
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        // Quick save com tecla 'G'
        if(c.command == 'G') {
            return CREATE_BACKUP;
        }

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
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
    
    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;

    // Parsear o ficheiro do nível
    level_data_t level_data;
    if (parse_level_file(level_directory, "1", &level_data) != 0) {
        printf("ERRO: Não consegui carregar o nível!\n");
        return 1;
    }

    while (!end_game) {
        load_level(&game_board, accumulated_points, &level_data);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            // Handler para criar backup
            if(result == CREATE_BACKUP) {
                // Verificar se já existe um backup
                if(backup_pid != -1) {
                    // Verificar se o processo filho ainda existe
                    int status;
                    pid_t check = waitpid(backup_pid, &status, WNOHANG);
                    if(check == 0) {
                        // Processo filho ainda está a correr - já existe backup
                        debug("Backup already exists, ignoring G key\n");
                        continue;
                    } else {
                        // Processo filho terminou, pode criar novo backup
                        backup_pid = -1;
                    }
                }

                // Criar novo processo de backup
                backup_pid = fork();
                
                if(backup_pid < 0) {
                    // Erro no fork
                    debug("ERROR: Fork failed\n");
                    continue;
                }
                else if(backup_pid == 0) {
                    // PROCESSO FILHO - Guarda o estado e fica em pausa
                    debug("CHILD: Backup created, waiting...\n");
                    
                    // O estado do jogo já está na memória do processo filho
                    // devido ao fork() que copiou todo o espaço de endereçamento
                    
                    // Ficar em pausa infinita até ser terminado ou receber sinal
                    while(1) {
                        pause(); // Espera por sinal
                    }
                    
                    // Nunca chega aqui, mas por segurança:
                    exit(0);
                }
                else {
                    // PROCESSO PAI - Continua o jogo normalmente
                    debug("PARENT: Backup process created with PID %d\n", backup_pid);
                }
                
                continue;
            }

            if(result == NEXT_LEVEL) {
                // Terminar processo de backup se existir
                if(backup_pid != -1) {
                    kill(backup_pid, SIGTERM);
                    waitpid(backup_pid, NULL, 0);
                    backup_pid = -1;
                }
                
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                break;
            }

            if(result == QUIT_GAME) {
                // Se morreu e existe backup, restaurar do processo filho
                if(backup_pid != -1) {
                    debug("PARENT: Pacman died, restoring from backup\n");
                    
                    // Terminar o processo pai (atual)
                    // O processo filho vai continuar e tornar-se o processo principal
                    
                    // Limpar recursos do processo pai
                    unload_level(&game_board);
                    terminal_cleanup();
                    close_debug_file();
                    
                    // Acordar o processo filho para que ele continue
                    kill(backup_pid, SIGCONT);
                    
                    // Terminar o processo pai
                    exit(0);
                }
                
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

    // Limpar processo de backup se ainda existir
    if(backup_pid != -1) {
        kill(backup_pid, SIGTERM);
        waitpid(backup_pid, NULL, 0);
    }

    terminal_cleanup();

    close_debug_file();

    return 0;
}