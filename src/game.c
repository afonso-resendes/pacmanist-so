#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>      // Para opendir, readdir, closedir
#include <string.h>      // Para strcmp, strlen, strncpy
#include <ctype.h>       // Para isdigit

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
        char level_name[32];
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
                // Guardar pontos acumulados
                accumulated_points = game_board.pacmans[0].points;
                
                // Só matar o pai se realmente existe um backup (processo pai suspenso)
                // Se playing_pid != -1, significa que somos o pai (backup)
                // Se playing_pid == -1 e is_backup == 0, somos o filho mas pode não haver backup
                // Só matamos o pai se realmente criámos um backup antes
                // NOTA: Esta lógica é do Exercício 2 (backup), não do Exercício 1
                // Para o Exercício 1, podemos simplesmente remover esta parte
                
                // Avançar para o próximo nível
                current_level_index++;
                
                // Verificar se há mais níveis
                if (current_level_index >= n_levels) {
                    // Último nível completado - mostrar VICTORY
                    printf("✓ Todos os níveis completados!\n");
                    screen_refresh(&game_board, DRAW_WIN);
                    sleep_ms(game_board.tempo);
                    end_game = true;
                } else {
                    // Ainda há mais níveis - apenas mostrar mensagem
                    printf("✓ Nível %s completado! Avançando para o próximo...\n", level_name);
                    // Limpar o ecrã antes de carregar o próximo nível
                    clear();
                }
                
                break;  // Sair do loop interno para carregar próximo nível
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
    
    // Se completou todos os níveis, mostrar mensagem final
    if (current_level_index >= n_levels && !end_game) {
        printf("🎉 Parabéns! Completaste todos os %d níveis!\n", n_levels);
        printf("Pontos finais: %d\n", accumulated_points);
    }

    terminal_cleanup();
    close_debug_file();

    return 0;
}
