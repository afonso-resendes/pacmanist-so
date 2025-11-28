#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

// Função para salvar o estado do jogo usando chamadas de sistema
int save_game_state(board_t* game_board, const char* level_name, int accumulated_points) {
    int fd = open("quicksave.dat", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        return -1;
    }

    // Salvar informações básicas
    write(fd, &game_board->rows, sizeof(int));
    write(fd, &game_board->cols, sizeof(int));
    write(fd, &accumulated_points, sizeof(int));
    
    // Salvar nome do nível
    int level_name_len = strlen(level_name) + 1;
    write(fd, &level_name_len, sizeof(int));
    write(fd, level_name, level_name_len);

    // Salvar tabuleiro
    for (int i = 0; i < game_board->rows; i++) {
        write(fd, game_board->board[i], game_board->cols);
    }

    // Salvar pacmans
    write(fd, &game_board->n_pacmans, sizeof(int));
    for (int i = 0; i < game_board->n_pacmans; i++) {
        write(fd, &game_board->pacmans[i], sizeof(pacman_t));
    }

    // Salvar ghosts
    write(fd, &game_board->n_ghosts, sizeof(int));
    for (int i = 0; i < game_board->n_ghosts; i++) {
        write(fd, &game_board->ghosts[i], sizeof(ghost_t));
    }

    close(fd);
    return 0;
}

// Função para carregar o estado do jogo usando chamadas de sistema
int load_game_state(board_t* game_board, char* level_name, int* accumulated_points) {
    int fd = open("quicksave.dat", O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    // Carregar informações básicas
    int rows, cols;
    read(fd, &rows, sizeof(int));
    read(fd, &cols, sizeof(int));
    read(fd, accumulated_points, sizeof(int));
    
    // Carregar nome do nível
    int level_name_len;
    read(fd, &level_name_len, sizeof(int));
    read(fd, level_name, level_name_len);

    game_board->rows = rows;
    game_board->cols = cols;

    // Alocar e carregar tabuleiro
    game_board->board = malloc(rows * sizeof(char*));
    for (int i = 0; i < rows; i++) {
        game_board->board[i] = malloc(cols * sizeof(char));
        read(fd, game_board->board[i], cols);
    }

    // Carregar pacmans
    read(fd, &game_board->n_pacmans, sizeof(int));
    game_board->pacmans = malloc(game_board->n_pacmans * sizeof(pacman_t));
    for (int i = 0; i < game_board->n_pacmans; i++) {
        read(fd, &game_board->pacmans[i], sizeof(pacman_t));
    }

    // Carregar ghosts
    read(fd, &game_board->n_ghosts, sizeof(int));
    game_board->ghosts = malloc(game_board->n_ghosts * sizeof(ghost_t));
    for (int i = 0; i < game_board->n_ghosts; i++) {
        read(fd, &game_board->ghosts[i], sizeof(ghost_t));
    }

    close(fd);
    return 0;
}

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

        // Quick load com tecla 'L'
        if(c.command == 'L') {
            return LOAD_BACKUP;
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
    char current_level[256] = "1";

    // Parsear o ficheiro do nível
    level_data_t level_data;
    if (parse_level_file(level_directory, current_level, &level_data) != 0) {
        printf("ERRO: Não consegui carregar o nível!\n");
        return 1;
    }

    while (!end_game) {
        load_level(&game_board, accumulated_points, &level_data);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        while(true) {
            int result = play_board(&game_board); 

            if(result == CREATE_BACKUP) {
                if(save_game_state(&game_board, current_level, accumulated_points) == 0) {
                    debug("Game saved successfully\n");
                }
                continue;
            }

            if(result == LOAD_BACKUP) {
                unload_level(&game_board);
                if(load_game_state(&game_board, current_level, &accumulated_points) == 0) {
                    debug("Game loaded successfully\n");
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();
                }
                continue;
            }

            if(result == NEXT_LEVEL) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(game_board.tempo);
                break;
            }

            if(result == QUIT_GAME) {
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