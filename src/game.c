#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>

#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

// PID do processo backup (pai que está suspenso)
static pid_t backup_parent_pid = -1;

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

        // Quick save com tecla 'G'
        if(c.command == 'G') {
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

    pacman_t* pacman_before = &game_board->pacmans[0];
    int pacman_x_before = pacman_before->pos_x;
    int pacman_y_before = pacman_before->pos_y;
    
    int result = move_pacman(game_board, 0, play);
    
    pacman_t* pacman_after = &game_board->pacmans[0];
    int pacman_x_after = pacman_after->pos_x;
    int pacman_y_after = pacman_after->pos_y;
    int pacman_actually_moved = (pacman_x_before != pacman_x_after || pacman_y_before != pacman_y_after);
    
    if (result == REACHED_PORTAL) {
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    // Only move ghosts when Pacman actually moved
    // This prevents ghosts from moving multiple times when Pacman is waiting
    // or still processing a multi-turn command
    if (pacman_actually_moved) {
        for (int i = 0; i < game_board->n_ghosts; i++) {
            ghost_t* ghost = &game_board->ghosts[i];
            move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
        }
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

// Função auxiliar para comparar dois nomes de níveis (para ordenação)
// Compara numericamente se ambos são números, senão alfabeticamente
static int compare_level_names(const void* a, const void* b) {
    const char* name1 = (const char*)a;
    const char* name2 = (const char*)b;
    
    // Verificar se ambos são números (todos os caracteres são dígitos)
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
    
    // Se ambos são números, comparar numericamente
    if (is_num1 && is_num2) {
        int num1 = atoi(name1);
        int num2 = atoi(name2);
        return num1 - num2;
    }
    
    // Senão, comparar alfabeticamente
    return strcmp(name1, name2);
}

// Função para ordenar os níveis encontrados
void sort_level_files(char level_names[][MAX_FILENAME], int n_levels) {
    qsort(level_names, n_levels, MAX_FILENAME, compare_level_names);
}

// Função para listar todos os ficheiros .lvl no diretório
// Retorna o número de níveis encontrados e preenche o array level_names
// level_names: array onde serão guardados os nomes dos níveis (sem extensão .lvl)
// Retorna: número de níveis encontrados, ou -1 em caso de erro
int find_level_files(char* level_directory, char level_names[][MAX_FILENAME]) {
    DIR* dir = opendir(level_directory);
    if (dir == NULL) {
        printf("ERRO: Não consegui abrir o diretório %s\n", level_directory);
        return -1;
    }
    
    int count = 0;
    struct dirent* entry;
    
    // Ler todas as entradas do diretório
    while ((entry = readdir(dir)) != NULL && count < MAX_LEVELS) {
        char* name = entry->d_name;
        size_t len = strlen(name);
        
        // Verificar se termina com .lvl (e tem pelo menos 5 caracteres: "x.lvl")
        if (len > 4 && strcmp(name + len - 4, ".lvl") == 0) {
            // Copiar o nome sem a extensão .lvl
            size_t name_len = len - 4;  // Comprimento sem ".lvl"
            if (name_len < MAX_FILENAME) {
                strncpy(level_names[count], name, name_len);
                level_names[count][name_len] = '\0';  // Garantir null-termination
                count++;
            }
        }
    }
    
    closedir(dir);
    
    if (count == 0) {
        printf("AVISO: Não foram encontrados ficheiros .lvl no diretório %s\n", level_directory);
        return 0;
    }
    
    printf("✓ Encontrados %d ficheiro(s) .lvl\n", count);
    return count;
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
    
    // Listar todos os ficheiros .lvl no diretório
    char level_names[MAX_LEVELS][MAX_FILENAME];
    int n_levels = find_level_files(level_directory, level_names);
    
    if (n_levels <= 0) {
        printf("ERRO: Não foram encontrados níveis para carregar!\n");
        terminal_cleanup();
        close_debug_file();
        return 1;
    }
    
    // Ordenar os níveis encontrados
    sort_level_files(level_names, n_levels);
    
    // Mostrar níveis encontrados (debug)
    printf("Níveis encontrados (ordenados):\n");
    for (int i = 0; i < n_levels; i++) {
        printf("  %d. %s.lvl\n", i + 1, level_names[i]);
    }
    printf("\n");
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;

    int current_level_index = 0;
    
    // Loop principal: carrega e joga cada nível sequencialmente
    while (!end_game && current_level_index < n_levels) {
        char level_name[MAX_FILENAME];  // MUDAR DE 32 PARA MAX_FILENAME
        level_data_t level_data;
        
        // Carregar o nível atual
        snprintf(level_name, sizeof(level_name), "%s", level_names[current_level_index]);
        printf("=== Carregando nível: %s.lvl ===\n", level_name);
        
        if (parse_level_file(level_directory, level_name, &level_data) != 0) {
            printf("ERRO: Não consegui carregar o nível %s!\n", level_name);
            break;  // Sair se não conseguir carregar
        }
        
        load_level(&game_board, accumulated_points, &level_data, level_directory);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        // Loop interno: jogar o nível atual
        while(true) {
            int result = play_board(&game_board); 

            // ========== EXERCÍCIO 2: QUICK SAVE COM FORK ==========
            if(result == CREATE_BACKUP) {
                debug("CURRENT: G key pressed (backup_parent_pid=%d)...\n", backup_parent_pid);
                
                // REGRA: Só pode existir 1 backup guardado
                if(backup_parent_pid != -1) {
                    // Verificar se o processo pai backup ainda está vivo
                    if(kill(backup_parent_pid, 0) == 0) {
                        debug("CURRENT: Backup already exists (PID %d), ignoring G key\n", backup_parent_pid);
                        continue; // Ignorar tecla G
                    } else {
                        // Pai morreu, limpar
                        backup_parent_pid = -1;
                    }
                }

                debug("CURRENT: Creating backup with fork()...\n");
                
                pid_t child_pid = fork();
                
                if(child_pid < 0) {
                    debug("ERROR: Fork failed\n");
                    continue;
                }
                else if(child_pid == 0) {
                    // PROCESSO FILHO - Continua a jogar
                    backup_parent_pid = getppid(); // Guardar PID do pai backup
                    
                    debug("CHILD: Created with PID %d, parent backup is PID %d\n", 
                          getpid(), backup_parent_pid);
                    
                    // Filho continua o loop normalmente
                    continue;
                }
                else {
                    // PROCESSO PAI - Torna-se o backup e suspende-se
                    debug("PARENT: Becoming backup (PID %d), child playing (PID %d)\n", 
                          getpid(), child_pid);
                    debug("PARENT: Suspending with SIGSTOP...\n");
                    fflush(NULL);
                    
                    // PAI suspende-se (GUARDA O ESTADO NA MEMÓRIA)
                    raise(SIGSTOP);
                    
                    // ===== QUANDO FILHO MORRE, PAI ACORDA AQUI =====
                    debug("PARENT: ====== BACKUP ACORDOU! ====== PID %d\n", getpid());
                    debug("PARENT: Child died, resuming from saved position...\n");
                    
                    // Ressuscitar o Pacman (estava morto)
                    game_board.pacmans[0].alive = true;
                    backup_parent_pid = -1; // Não somos mais backup de ninguém
                    
                    // Redesenhar o tabuleiro no estado GUARDADO
                    clear();
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();
                    
                    debug("PARENT: Resumed gameplay from backup state\n");
                    
                    // Continuar o jogo do ponto guardado
                    continue;
                }
            }

            if(result == NEXT_LEVEL) {
                accumulated_points = game_board.pacmans[0].points;
                
                // Se temos um pai backup, matar (não precisamos mais)
                if(backup_parent_pid != -1) {
                    debug("CHILD: Next level, killing backup parent %d\n", backup_parent_pid);
                    kill(backup_parent_pid, SIGKILL);
                    waitpid(backup_parent_pid, NULL, 0);
                    backup_parent_pid = -1;
                }
                
                current_level_index++;
                
                if (current_level_index >= n_levels) {
                    printf("✓ Todos os níveis completados!\n");
                    screen_refresh(&game_board, DRAW_WIN);
                    sleep_ms(game_board.tempo);
                    end_game = true;
                } else {
                    printf("✓ Nível %s completado! Avançando para o próximo...\n", level_name);
                    clear();
                }
                
                break;
            }

            if(result == QUIT_GAME) {
                // Se temos um pai backup, acordá-lo
                if(backup_parent_pid != -1) {
                    debug("CHILD: Died, waking backup parent %d\n", backup_parent_pid);
                    
                    // Verificar se o pai ainda existe
                    if(kill(backup_parent_pid, 0) == 0) {
                        // Acordar o pai
                        kill(backup_parent_pid, SIGCONT);
                        
                        // Filho termina (pai continua)
                        unload_level(&game_board);

                        close_debug_file();
                        exit(0);
                    } else {
                        debug("CHILD: Backup parent died, no restoration possible\n");
                        backup_parent_pid = -1;
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
    
    // Se completou todos os níveis, mostrar mensagem final
    if (current_level_index >= n_levels && !end_game) {
        printf("🎉 Parabéns! Completaste todos os %d níveis!\n", n_levels);
        printf("Pontos finais: %d\n", accumulated_points);
    }

    terminal_cleanup();
    close_debug_file();

    return 0;
}
