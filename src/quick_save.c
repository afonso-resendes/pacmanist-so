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
// Flag para indicar se este processo é o backup
static int is_backup = 0;

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    debug("PLAY_BOARD: Function called\n");
    
    pacman_t* pacman = &game_board->pacmans[0];
    
    debug("PLAY_BOARD: pacman->n_moves = %d\n", pacman->n_moves);
    
    command_t* play;
    if (pacman->n_moves == 0) {
        debug("PLAY_BOARD: Getting user input...\n");
        command_t c; 
        c.command = get_input();
        
        debug("PLAY_BOARD: get_input() returned: '%c' (0x%02x)\n", c.command ? c.command : '?', (unsigned char)c.command);

        if(c.command == '\0')
            return CONTINUE_PLAY;

        // Quick save com tecla 'G' - apenas se não for processo de backup
        if(c.command == 'G' && !is_backup) {
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

        // Se este processo é um backup que acabou de acordar
        if(is_backup) {
            debug("CHILD: Restarting game loop from saved state\n");
        }

        debug("MAIN: Entering inner game loop (while true)\n");

        while(true) {
            debug("MAIN: Calling play_board()\n");
            int result = play_board(&game_board); 

            debug("MAIN: play_board() returned %d\n", result);

            // Handler para criar backup - apenas no processo pai
            if(result == CREATE_BACKUP && !is_backup) {
                debug("PARENT: G key pressed, attempting to create backup...\n");
                
                // Verificar se já existe um backup
                if(backup_pid != -1) {
                    // Verificar se o processo filho ainda existe
                    int status;
                    pid_t check = waitpid(backup_pid, &status, WNOHANG);
                    if(check == 0) {
                        // Processo filho ainda está a correr - já existe backup
                        debug("PARENT: Backup already exists (PID %d), ignoring G key\n", backup_pid);
                        continue;
                    } else {
                        // Processo filho terminou, pode criar novo backup
                        debug("PARENT: Previous backup died, clearing backup_pid\n");
                        backup_pid = -1;
                    }
                }

                debug("PARENT: Creating new backup process with fork()...\n");
                
                // Criar novo processo de backup
                backup_pid = fork();
                
                if(backup_pid < 0) {
                    // Erro no fork
                    debug("ERROR: Fork failed\n");
                    continue;
                }
                else if(backup_pid == 0) {
                    // PROCESSO FILHO - Guarda o estado e fica suspenso
                    is_backup = 1;
                    backup_pid = -1;
                    
                    debug("CHILD: Backup created with PID %d, suspending with SIGSTOP...\n", getpid());
                    fflush(NULL);
                    
                    // Suspender o processo até receber SIGCONT
                    raise(SIGSTOP);
                    
                    // Quando acordar (via SIGCONT), continua EXATAMENTE daqui
                    debug("CHILD: ====== ACORDEI! ====== PID %d\n", getpid());
                    debug("CHILD: Resumed from backup, continuing from saved position...\n");
                    
                    // IMPORTANTE: Forçar modo blocking no getch()
                    nodelay(stdscr, FALSE);  // Desativar non-blocking
                    timeout(-1);              // Timeout infinito
                    
                    // Ressuscitar o Pacman
                    game_board.pacmans[0].alive = true;
                    debug("CHILD: Pacman marked as alive\n");
                    
                    // Redesenhar o ecrã no estado GUARDADO
                    debug("CHILD: About to redraw screen...\n");
                    clear();
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();
                    
                    debug("CHILD: Screen redrawn in BLOCKING mode, resuming gameplay...\n");
                    
                    // Continuar o loop
                    continue;
                }
                else {
                    // PROCESSO PAI - Continua o jogo normalmente
                    debug("PARENT: Backup process created with PID %d\n", backup_pid);
                }
                
                continue;
            }

            if(result == NEXT_LEVEL) {
                // Terminar processo de backup se existir
                if(backup_pid != -1 && !is_backup) {
                    debug("PARENT: Next level, killing backup process %d\n", backup_pid);
                    kill(backup_pid, SIGKILL);
                    waitpid(backup_pid, NULL, 0);
                    backup_pid = -1;
                }
                
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                break;
            }

            if(result == QUIT_GAME) {
                // Se morreu e existe backup, acordar o processo filho
                if(backup_pid != -1 && !is_backup) {
                    debug("PARENT: Pacman died, waking backup process %d\n", backup_pid);
                    fflush(NULL); // Forçar escrita
                    
                    // Acordar o processo filho
                    kill(backup_pid, SIGCONT);
                    
                    debug("PARENT: Sent SIGCONT to backup, now terminating parent process\n");
                    fflush(NULL);
                    
                    // Pequeno delay para garantir que o sinal foi recebido
                    sleep_ms(100); // 100ms
                    
                    // Limpar apenas a memória do jogo
                    unload_level(&game_board);
                    close_debug_file();
                    
                    // Terminar o processo pai SEM fechar o terminal
                    exit(0);
                }
                
                // Se não há backup ou se este é o backup que morreu
                screen_refresh(&game_board, DRAW_GAME_OVER); 
                sleep_ms(game_board.tempo);
                end_game = true;
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 

            accumulated_points = game_board.pacmans[0].points;      
        }
        
        debug("MAIN: Exited inner loop, cleaning up\n");
        print_board(&game_board);
        unload_level(&game_board);
    }    

    // Limpar processo de backup se ainda existir
    if(backup_pid != -1) {
        debug("Cleaning up backup process %d\n", backup_pid);
        kill(backup_pid, SIGKILL);
        waitpid(backup_pid, NULL, 0);
    }    

    terminal_cleanup();

    close_debug_file();

    return 0;
}
