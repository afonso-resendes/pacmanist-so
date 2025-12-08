#include "board.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>      // Para open(), O_RDONLY
#include <stdarg.h>
#include <string.h> 
#include <stdbool.h> 


FILE * debugfile;

// Helper private function to find and kill pacman at specific position
static int find_and_kill_pacman(board_t* board, int new_x, int new_y) {
    for (int p = 0; p < board->n_pacmans; p++) {
        pacman_t* pac = &board->pacmans[p];
        if (pac->pos_x == new_x && pac->pos_y == new_y && pac->alive) {
            pac->alive = 0;
            kill_pacman(board, p);
            return DEAD_PACMAN;
        }
    }
    return VALID_MOVE;
}

// Helper private function for getting board position index
static inline int get_board_index(board_t* board, int x, int y) {
    return y * board->width + x;
}

// Helper private function for checking valid position
static inline int is_valid_position(board_t* board, int x, int y) {
    return (x >= 0 && x < board->width) && (y >= 0 && y < board->height); // Inside of the board boundaries
}

static int parse_dim_line(char* line, int* width, int* height) {
    if (strncmp(line, "DIM", 3) != 0) {
        return -1;
    }

    if (sscanf(line, "DIM %d %d", width, height) != 2) {
        return -1; // Invalid format
    }

    return 0; 
}

static int parse_tempo_line(char* line, int* tempo) {
    if (strncmp(line, "TEMPO", 5) != 0) {
        return -1;
    }

    if (sscanf(line, "TEMPO %d", tempo) != 1) {
        return -1; // Invalid format
    }

    return 0;
}

static int parse_pac_line(char* line, char* pac_file) {
    if (strncmp(line, "PAC", 3) != 0) {
        return -1;
    }
    if (sscanf(line, "PAC %s", pac_file) != 1) {
        return -1; // Invalid format
    }

    return 0;
}

static int parse_mon_line(char* line, char ghost_files[][MAX_FILENAME], int* n_ghosts) {
    if (strncmp(line, "MON", 3) != 0) {
        return -1;
    }

    char* start = line + 3;

    while (*start == ' ' || *start == '\t') {
        start++;
    }

    if (*start == '\0') {
        return -1; // Invalid format
    }

    *n_ghosts = 0;
    char* token = start;

    while (*token != '\0' && *n_ghosts < MAX_GHOSTS) {

        while(*token == ' '|| *token == '\t') {
            token++;
        }

        if (*token == '\0') {
            break;
        }
        
        int i = 0;
        while (*token != '\0' && *token != ' ' && *token != '\t' && i < MAX_FILENAME - 1) {
            ghost_files[*n_ghosts][i] = *token;
            i++;
            token++;
        }
        ghost_files[*n_ghosts][i] = '\0';
        (*n_ghosts)++;
    }
    if (*n_ghosts == 0) {
        return -1; // Invalid format
    }
    return 0;      
}

int parse_monster_file(char* level_directory, char* monster_file, ghost_t* ghost) {

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", level_directory, monster_file);

    int fd = open(file_path, O_RDONLY);
    if (fd <0) {
         printf("ERRO: Não consegui abrir ficheiro de monstro: %s\n", file_path);
        return -1;
    }
    char buffer[4096];
    ssize_t total_bytes = read(fd, buffer, sizeof(buffer) -1);
    if (total_bytes < 0) {
        close(fd);
        return -1;
    }

    buffer[total_bytes] ='\0';
    close(fd);
    
    // IMPORTANTE: Inicializar pos_x e pos_y ANTES de qualquer outra coisa
    ghost->pos_x = -1; 
    ghost->pos_y = -1;
    ghost->passo = 0;
    ghost->waiting = 0;
    ghost->current_move = 0;
    ghost->n_moves = 0;
    ghost->charged = 0; 

    // Parsear linha por linha
    char* line_start = buffer;
    char* current = buffer;
    bool found_passo = false;
    bool found_pos = false;
    bool in_commands = false; // Flag para saber se já passámos PASSO e POS
    
    while (*current != '\0') {
        if (*current == '\n') {
            *current = '\0';
            
            if (current > line_start) {
                // Ignorar linhas vazias e comentários (começadas com #)
                if (line_start[0] == '\0' || line_start[0] == '#') {
                    line_start = current + 1;
                    current++;  // Avançar current antes do continue
                    continue;
                }
                
                // Parsear PASSO (só no início, antes dos comandos)
                if (strncmp(line_start, "PASSO", 5) == 0 && !found_passo && !in_commands) {
                    if (sscanf(line_start, "PASSO %d", &ghost->passo) == 1) {
                        found_passo = true;
                    }
                }
                // Parsear POS (só no início, antes dos comandos)
                else if (strncmp(line_start, "POS", 3) == 0 && !found_pos && !in_commands) {
                    int row, col;
                    if (sscanf(line_start, "POS %d %d", &row, &col) == 2) {
                        ghost->pos_y = row;
                        ghost->pos_x = col;
                        found_pos = true;
                    }
                }
                // Comandos de movimento (após PASSO e POS, ou se não existirem)
                else {
                    in_commands = true; // A partir daqui são comandos
                    
                    // Parsear comando: formato "COMANDO N" ou apenas "COMANDO"
                    char cmd = '\0';
                    int turns = 1;
                    
                    // Tentar ler comando com número (ex: "D 8", "T 5")
                    if (sscanf(line_start, "%c %d", &cmd, &turns) == 2) {
                        // Comando com número de turns
                        if (ghost->n_moves < MAX_MOVES) {
                            ghost->moves[ghost->n_moves].command = cmd;
                            ghost->moves[ghost->n_moves].turns = turns;
                            ghost->moves[ghost->n_moves].turns_left = turns;
                            ghost->n_moves++;
                        }
                    } 
                    // Tentar ler apenas comando (ex: "R", "C")
                    else if (sscanf(line_start, "%c", &cmd) == 1) {
                        if (ghost->n_moves < MAX_MOVES) {
                            ghost->moves[ghost->n_moves].command = cmd;
                            ghost->moves[ghost->n_moves].turns = 1;
                            ghost->moves[ghost->n_moves].turns_left = 1;
                            ghost->n_moves++;
                        }
                    }
                }
            }
            
            line_start = current + 1;
        }
        current++;
    }

    return 0;
}

int parse_pacman_file(char* level_directory, char* pacman_file, pacman_t* pacman) {

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", level_directory, pacman_file);

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        printf("ERRO: Não consegui abrir ficheiro de pacman: %s\n", file_path);
        return -1;
    }
    char buffer[4096];
    ssize_t total_bytes = read(fd, buffer, sizeof(buffer) - 1);
    if (total_bytes < 0) {
        close(fd);
        return -1;
    }

    buffer[total_bytes] = '\0';
    close(fd);
    
    // IMPORTANTE: Inicializar pos_x e pos_y ANTES de qualquer outra coisa
    pacman->pos_x = -1; 
    pacman->pos_y = -1;
    pacman->passo = 0;
    pacman->waiting = 0;
    pacman->current_move = 0;
    pacman->n_moves = 0;
    pacman->alive = 1;  // Pacman começa vivo
    // points não é inicializado aqui, será definido em load_pacman()

    // Parsear linha por linha
    char* line_start = buffer;
    char* current = buffer;
    bool found_passo = false;
    bool found_pos = false;
    bool in_commands = false; // Flag para saber se já passámos PASSO e POS
    
    while (*current != '\0') {
        if (*current == '\n') {
            *current = '\0';
            
            if (current > line_start) {
                // Ignorar linhas vazias e comentários (começadas com #)
                if (line_start[0] == '\0' || line_start[0] == '#') {
                    line_start = current + 1;
                    current++;  // Avançar current antes do continue
                    continue;
                }
                
                // Parsear PASSO (só no início, antes dos comandos)
                if (strncmp(line_start, "PASSO", 5) == 0 && !found_passo && !in_commands) {
                    if (sscanf(line_start, "PASSO %d", &pacman->passo) == 1) {
                        found_passo = true;
                    }
                }
                // Parsear POS (só no início, antes dos comandos)
                else if (strncmp(line_start, "POS", 3) == 0 && !found_pos && !in_commands) {
                    int row, col;
                    if (sscanf(line_start, "POS %d %d", &row, &col) == 2) {
                        pacman->pos_y = row;
                        pacman->pos_x = col;
                        found_pos = true;
                    }
                }
                // Comandos de movimento (após PASSO e POS, ou se não existirem)
                else {
                    in_commands = true; // A partir daqui são comandos
                    
                    // Parsear comando: formato "COMANDO N" ou apenas "COMANDO"
                    char cmd = '\0';
                    int turns = 1;
                    
                    // Tentar ler comando com número (ex: "D 8", "T 5")
                    if (sscanf(line_start, "%c %d", &cmd, &turns) == 2) {
                        // Comando com número de turns
                        if (pacman->n_moves < MAX_MOVES) {
                            pacman->moves[pacman->n_moves].command = cmd;
                            pacman->moves[pacman->n_moves].turns = turns;
                            pacman->moves[pacman->n_moves].turns_left = turns;
                            pacman->n_moves++;
                        }
                    } 
                    // Tentar ler apenas comando (ex: "R")
                    else if (sscanf(line_start, "%c", &cmd) == 1) {
                        if (pacman->n_moves < MAX_MOVES) {
                            pacman->moves[pacman->n_moves].command = cmd;
                            pacman->moves[pacman->n_moves].turns = 1;
                            pacman->moves[pacman->n_moves].turns_left = 1;
                            pacman->n_moves++;
                        }
                    }
                }
            }
            
            line_start = current + 1;
        }
        current++;
    }

    return 0;
}

void sleep_ms(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}


int move_pacman(board_t* board, int pacman_index, command_t* command) {
    if (pacman_index < 0 || !board->pacmans[pacman_index].alive) {
        return DEAD_PACMAN; // Invalid or dead pacman
    }

    pacman_t* pac = &board->pacmans[pacman_index];
    int new_x = pac->pos_x;
    int new_y = pac->pos_y;

    // check passo - só aplicar a comandos de movimento, não a T
    if (command->command != 'T') {
        if (pac->waiting > 0) {
            pac->waiting -= 1;
            return VALID_MOVE;        
        }
        pac->waiting = pac->passo;
    }

    char direction = command->command;

    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'T': // Wait
            if (command->turns_left == 1) {
                pac->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    // Decrementar turns_left e só avançar para próximo comando quando chegar a 0
    command->turns_left--;
    if (command->turns_left <= 0) {
        pac->current_move++;
        command->turns_left = command->turns;  // Reset para o próximo ciclo
    }

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, pac->pos_x, pac->pos_y);
    char target_content = board->board[new_index].content;

    if (board->board[new_index].has_portal) {
        board->board[old_index].content = ' ';
        board->board[new_index].content = 'P';
        return REACHED_PORTAL;
    }

    // Check for walls
    if (target_content == 'W') {
        return INVALID_MOVE;
    }

    // Check for ghosts
    if (target_content == 'M') {
        kill_pacman(board, pacman_index);
        return DEAD_PACMAN;
    }

    // Collect points
    if (board->board[new_index].has_dot) {
        pac->points++;
        board->board[new_index].has_dot = 0;
    }

    board->board[old_index].content = ' ';
    pac->pos_x = new_x;
    pac->pos_y = new_y;
    board->board[new_index].content = 'P';

    return VALID_MOVE;
}

// Helper private function for charged ghost movement in one direction
static int move_ghost_charged_direction(board_t* board, ghost_t* ghost, char direction, int* new_x, int* new_y) {
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    *new_x = x;
    *new_y = y;
    
    switch (direction) {
        case 'W': // Up
            if (y == 0) return INVALID_MOVE;
            *new_y = 0; // In case there is no colision
            for (int i = y - 1; i >= 0; i--) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i + 1; // stop before colision
                    return VALID_MOVE;
                }
                else if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'S': // Down
            if (y == board->height - 1) return INVALID_MOVE;
            *new_y = board->height - 1; // In case there is no colision
            for (int i = y + 1; i < board->height; i++) {
                char target_content = board->board[get_board_index(board, x, i)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_y = i - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_y = i;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'A': // Left
            if (x == 0) return INVALID_MOVE;
            *new_x = 0; // In case there is no colision
            for (int j = x - 1; j >= 0; j--) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j + 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;

        case 'D': // Right
            if (x == board->width - 1) return INVALID_MOVE;
            *new_x = board->width - 1; // In case there is no colision
            for (int j = x + 1; j < board->width; j++) {
                char target_content = board->board[get_board_index(board, j, y)].content;
                if (target_content == 'W' || target_content == 'M') {
                    *new_x = j - 1; // stop before colision
                    return VALID_MOVE;
                }
                if (target_content == 'P') {
                    *new_x = j;
                    return find_and_kill_pacman(board, *new_x, *new_y);
                }
            }
            break;
        default:
            debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
            return INVALID_MOVE;
    }
    return VALID_MOVE;
}   

int move_ghost_charged(board_t* board, int ghost_index, char direction) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int x = ghost->pos_x;
    int y = ghost->pos_y;
    int new_x = x;
    int new_y = y;

    ghost->charged = 0; //uncharge
    int result = move_ghost_charged_direction(board, ghost, direction, &new_x, &new_y);
    if (result == INVALID_MOVE) {
        debug("DEFAULT CHARGED MOVE - direction = %c\n", direction);
        return INVALID_MOVE;
    }

    // Get board indices
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    int new_index = get_board_index(board, new_x, new_y);

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one
    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;
    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

int move_ghost(board_t* board, int ghost_index, command_t* command) {
    ghost_t* ghost = &board->ghosts[ghost_index];
    int new_x = ghost->pos_x;
    int new_y = ghost->pos_y;

    // check passo - só aplicar a comandos de movimento, não a T ou C
    if (command->command != 'T' && command->command != 'C') {
        if (ghost->waiting > 0) {
            ghost->waiting -= 1;
            return VALID_MOVE;
        }
        ghost->waiting = ghost->passo;
    }

    char direction = command->command;
    
    if (direction == 'R') {
        char directions[] = {'W', 'S', 'A', 'D'};
        direction = directions[rand() % 4];
    }

    // Calculate new position based on direction
    switch (direction) {
        case 'W': // Up
            new_y--;
            break;
        case 'S': // Down
            new_y++;
            break;
        case 'A': // Left
            new_x--;
            break;
        case 'D': // Right
            new_x++;
            break;
        case 'C': // Charge
            ghost->current_move += 1;
            ghost->charged = 1;
            return VALID_MOVE;
        case 'T': // Wait
            if (command->turns_left == 1) {
                ghost->current_move += 1; // move on
                command->turns_left = command->turns;
            }
            else command->turns_left -= 1;
            return VALID_MOVE;
        default:
            return INVALID_MOVE; // Invalid direction
    }

    // Logic for the WASD movement
    // Decrementar turns_left e só avançar para próximo comando quando chegar a 0
    command->turns_left--;
    if (command->turns_left <= 0) {
        ghost->current_move++;
        command->turns_left = command->turns;  // Reset para o próximo ciclo
    }
    if (ghost->charged)
        return move_ghost_charged(board, ghost_index, direction);

    // Check boundaries
    if (!is_valid_position(board, new_x, new_y)) {
        return INVALID_MOVE;
    }

    // Check board position
    int new_index = get_board_index(board, new_x, new_y);
    int old_index = get_board_index(board, ghost->pos_x, ghost->pos_y);
    char target_content = board->board[new_index].content;

    // Check for walls and ghosts
    if (target_content == 'W' || target_content == 'M') {
        return INVALID_MOVE;
    }

    int result = VALID_MOVE;
    // Check for pacman
    if (target_content == 'P') {
        result = find_and_kill_pacman(board, new_x, new_y);
    }

    // Update board - clear old position (restore what was there)
    board->board[old_index].content = ' '; // Or restore the dot if ghost was on one

    // Update ghost position
    ghost->pos_x = new_x;
    ghost->pos_y = new_y;

    // Update board - set new position
    board->board[new_index].content = 'M';
    return result;
}

void kill_pacman(board_t* board, int pacman_index) {
    debug("Killing %d pacman\n\n", pacman_index);
    pacman_t* pac = &board->pacmans[pacman_index];
    int index = pac->pos_y * board->width + pac->pos_x;

    // Remove pacman from the board
    board->board[index].content = ' ';

    // Mark pacman as dead
    pac->alive = 0;
}

// Static Loading
int load_pacman(board_t* board, int points, char* level_directory) {
    pacman_t* pac = &board->pacmans[0];
    
    // Se existe ficheiro .p, fazer parsing dinâmico
    if (board->pacman_file[0] != '\0') {
        if (parse_pacman_file(level_directory, board->pacman_file, pac) != 0) {
            printf("ERRO: Não consegui carregar pacman %s\n", board->pacman_file);
            return -1;
        }
        
        // Se POS foi especificado, colocar Pacman nessa posição
        if (pac->pos_x >= 0 && pac->pos_y >= 0) {
            int idx = pac->pos_y * board->width + pac->pos_x;
            if (idx >= 0 && idx < board->width * board->height) {
                board->board[idx].content = 'P';
                // Coletar ponto se existir na posição inicial
                if (board->board[idx].has_dot) {
                    pac->points++;
                    board->board[idx].has_dot = 0;
                }
            }
        } else {
            // Se POS não foi especificado, procurar 'P' no tabuleiro
            // (implementar depois se necessário)
            int idx = 1 * board->width + 1;
            board->board[idx].content = 'P'; // Fallback
            pac->pos_x = 1;
            pac->pos_y = 1;
            // Coletar ponto se existir na posição inicial
            if (board->board[idx].has_dot) {
                pac->points++;
                board->board[idx].has_dot = 0;
            }
        }
    } else {
        // Sem ficheiro .p, usar posição padrão (controlo manual)
        int idx = 1 * board->width + 1;
        board->board[idx].content = 'P';
        pac->pos_x = 1;
        pac->pos_y = 1;
        pac->n_moves = 0; // Controlo manual
        // Coletar ponto se existir na posição inicial
        if (board->board[idx].has_dot) {
            pac->points++;
            board->board[idx].has_dot = 0;
        }
    }
    
    pac->alive = 1;
    pac->points = points;
    
    // Inicializar waiting com passo para que o Pacman espere antes do primeiro movimento
    if (pac->n_moves > 0) {
        pac->waiting = pac->passo;
    }
    
    return 0;
}



int load_ghost(board_t* board, char* level_directory, level_data_t* level_data) {

    if (board->n_ghosts == 0) {
        return 0;
    }

    for (int i = 0; i < board->n_ghosts && i < MAX_GHOSTS; i++) {
        if (board->ghosts_files[i][0] != '\0') {

            if (parse_monster_file(level_directory, board->ghosts_files[i], &board->ghosts[i]) != 0) {
               printf("ERRO: Não consegui carregar monstro %s\n", board->ghosts_files[i]);
                return -1;
            }
            
            if (board->ghosts[i].pos_x >= 0 && board->ghosts[i].pos_y >= 0) {
                // POS foi especificado no ficheiro .m
                int idx = board->ghosts[i].pos_y * board->width + board->ghosts[i].pos_x;
                if (idx >= 0 && idx < board->width * board->height) {
                    board->board[idx].content = 'M';
                }
            } else {
                // POS não foi especificado, procurar 'M' no layout original do tabuleiro
                for (int y = 0; y < level_data->n_board_lines; y++) {
                    char* line = level_data->board_lines[y];
                    size_t line_len = strlen(line);
                    for (int x = 0; x < (int)line_len && x < board->width; x++) {
                        if (line[x] == 'M') {
                            // Encontrou 'M' no layout, colocar monstro aqui
                            int idx = y * board->width + x;
                            if (idx >= 0 && idx < board->width * board->height) {
                                board->board[idx].content = 'M';
                                board->ghosts[i].pos_x = x;
                                board->ghosts[i].pos_y = y;
                                break; // Encontrou, sair do loop interno
                            }
                        }
                    }
                    if (board->ghosts[i].pos_x >= 0) break; // Já encontrou, sair do loop externo
                }
            }
        }
    }
    return 0;
}

int load_level(board_t *board, int points, level_data_t* level_data, char* level_directory) {
    board->height = level_data->height;
    board->width = level_data->width;
    board->tempo = level_data->tempo;

    if (level_data->pac_file[0] != '\0') {
        strncpy(board->pacman_file, level_data->pac_file, sizeof(board->pacman_file) - 1);
        board->pacman_file[sizeof(board->pacman_file) - 1] = '\0';
    } else {
       board->pacman_file[0] = '\0';
    }
   

    board->n_ghosts = level_data->n_ghosts;
    board->n_pacmans = 1;

    board->board = calloc(board->width * board->height, sizeof(board_pos_t));
    board->pacmans = calloc(board->n_pacmans, sizeof(pacman_t));
    board->ghosts = calloc(board->n_ghosts, sizeof(ghost_t));

    for (int i = 0; i < board->n_ghosts && i < MAX_GHOSTS; i++) {
        strncpy(board->ghosts_files[i], level_data->ghost_files[i], sizeof(board->ghosts_files[i]) - 1);
        board->ghosts_files[i][sizeof(board->ghosts_files[i]) -1] = '\0';
    }

    sprintf(board->level_name, "Level %s", level_data->level_name);

    // Processar as linhas do tabuleiro do ficheiro
    for (int i = 0; i < level_data->n_board_lines && i < board->height; i++) {
        char* line = level_data->board_lines[i];
        int line_len = strlen(line);
        
        for (int j = 0; j < line_len && j < board->width; j++) {
            int idx = i * board->width + j;
            char ch = line[j];
            
            // Inicializar valores padrão
            board->board[idx].content = ' ';
            board->board[idx].has_dot = 0;
            board->board[idx].has_portal = 0;
            
            // Processar cada caractere
            if (ch == 'X') {
                board->board[idx].content = 'W';  // Parede
            } else if (ch == 'o') {
                board->board[idx].content = ' ';
                board->board[idx].has_dot = 1;  // Dot
            } else if (ch == '@') {
                board->board[idx].content = ' ';
                board->board[idx].has_portal = 1;  // Portal
            } else if (ch == 'P') {
                // Por agora, espaço vazio (depois colocamos o pacman aqui)
                board->board[idx].content = ' ';
            } else if (ch == 'M') {
                // Por agora, espaço vazio (depois colocamos o ghost aqui)
                board->board[idx].content = ' ';
            } else if (ch == ' ') {
                // Espaço vazio
                board->board[idx].content = ' ';
            }
        }
    }

    load_ghost(board, level_directory, level_data);
    load_pacman(board, points, level_directory);  // Adicionar level_directory

    return 0;
}

void unload_level(board_t * board) {
    free(board->board);
    free(board->pacmans);
    free(board->ghosts);
}

void open_debug_file(char *filename) {
    debugfile = fopen(filename, "w");
}

void close_debug_file() {
    fclose(debugfile);
}

void debug(const char * format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(debugfile, format, args);
    va_end(args);

    fflush(debugfile);
}

void print_board(board_t *board) {
    if (!board || !board->board) {
        debug("[%d] Board is empty or not initialized.\n", getpid());
        return;
    }

    // Large buffer to accumulate the whole output
    char buffer[8192];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "=== [%d] LEVEL INFO ===\n"
                       "Dimensions: %d x %d\n"
                       "Tempo: %d\n"
                       "Pacman file: %s\n",
                       getpid(), board->height, board->width, board->tempo, board->pacman_file);

    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                       "Monster files (%d):\n", board->n_ghosts);

    for (int i = 0; i < board->n_ghosts; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "  - %s\n", board->ghosts_files[i]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n=== BOARD ===\n");

    for (int y = 0; y < board->height; y++) {
        for (int x = 0; x < board->width; x++) {
            int idx = y * board->width + x;
            if (offset < sizeof(buffer) - 2) {
                buffer[offset++] = board->board[idx].content;
            }
        }
        if (offset < sizeof(buffer) - 2) {
            buffer[offset++] = '\n';
        }
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "==================\n");

    buffer[offset] = '\0';

    debug("%s", buffer);
}

// Helper function para processar uma linha
static void process_line(char* line, int line_num, level_data_t* level_data) {
     
    if (line[0] == '#') {
        return;
    }
    // Tentar parsear DIM
    if (parse_dim_line(line, &level_data->width, &level_data->height) == 0) {
        printf("✓ Linha %d - DIM: %d x %d\n", line_num, level_data->width, level_data->height);
        return; // Encontrou DIM, sair
    }

    if (parse_tempo_line(line, &level_data->tempo) == 0) {
        printf("✓ Linha %d - TEMPO: %d\n", line_num, level_data->tempo);
        return;
    }

    if (parse_pac_line(line, level_data->pac_file) == 0) {
        printf("✓ Linha %d - PAC: %s\n", line_num, level_data->pac_file);
        return;
    }

    if (parse_mon_line(line, level_data->ghost_files,&level_data->n_ghosts) == 0) {
        printf("✓ Linha %d - MON: %d ficheiros\n", line_num, level_data->n_ghosts);
        for (int i = 0; i < level_data->n_ghosts; i++) {
            printf("  - %s\n", level_data->ghost_files[i]);
        }
        return;
    }

    // Se não é nenhum comando conhecido, é uma linha do tabuleiro
    if (level_data->n_board_lines < MAX_BOARD_HEIGHT) {
        strncpy(level_data->board_lines[level_data->n_board_lines], 
               line, 
               MAX_FILENAME - 1);
        level_data->board_lines[level_data->n_board_lines][MAX_FILENAME - 1] = '\0';
        level_data->n_board_lines++;
        printf("✓ Linha %d - Tabuleiro: [%s]\n", line_num, line);
    }
}

int parse_level_file(char* level_directory, char* level_name, level_data_t* level_data) {
    // Criar estrutura para guardar dados parseados
   
    memset(level_data, 0, sizeof(level_data_t)); // Inicializar a zero

    strncpy(level_data->level_name, level_name, sizeof(level_data->level_name) -1);
    
    char level_path[512];
    snprintf(level_path, sizeof(level_path), "%s/%s.lvl", 
             level_directory, level_name);
    
    printf("Tentando abrir: %s\n", level_path);
    int fd = open(level_path, O_RDONLY);
    if (fd < 0) {
        printf("ERRO: Não consegui abrir o ficheiro!\n");
        return -1;
    }
    printf("✓ Ficheiro aberto com sucesso!\n\n");

    char buffer[4096];
    ssize_t total_bytes = read(fd, buffer, sizeof(buffer) - 1);

    if (total_bytes < 0) {
        printf("ERRO: Não consegui ler o ficheiro!\n");
        close(fd);
        return -1;
    }

    buffer[total_bytes] = '\0';

    int line_num = 0;
    char* line_start = buffer;
    char* current = buffer;

    while (*current != '\0') {
        if (*current == '\n') {
            *current = '\0';
            
            if (current > line_start) {
                line_num++;
                process_line(line_start, line_num, level_data);
            }
            
            line_start = current + 1;
        }
        current++;
    }
    
    // Processar última linha (se não terminar com \n)
    if (current > line_start) {
        line_num++;
        process_line(line_start, line_num, level_data);
    }   
    close(fd);
    printf("\n✓ Ficheiro lido completamente!\n");
    printf("Dimensões parseadas: %d x %d\n", level_data->width, level_data->height);
    printf("Linhas do tabuleiro: %d\n", level_data->n_board_lines);
    return 0;
}