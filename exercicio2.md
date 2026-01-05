---
name: Exercício 2 - Signal Handler SIGUSR1
overview: Implementar signal handler para SIGUSR1 que gera ficheiro de log com top 5 clientes por pontuação. Apenas thread anfitriã recebe o sinal, todas as outras threads bloqueiam SIGUSR1.
todos:
  - id: ex2-1
    content: Adicionar client_id à estrutura session_t para identificar clientes
    status: pending
  - id: ex2-2
    content: Criar flag global volatile int sigusr1_received para memorizar receção do sinal
    status: pending
  - id: ex2-3
    content: Criar função sigusr1_handler() que apenas define flag (sem I/O pesado)
    status: pending
  - id: ex2-4
    content: Registar signal handler no main() usando signal() ou sigaction()
    status: pending
  - id: ex2-5
    content: Garantir que apenas thread anfitriã recebe SIGUSR1 (main thread ou host thread)
    status: pending
  - id: ex2-6
    content: Usar pthread_sigmask(SIG_BLOCK) em todas threads gestoras para bloquear SIGUSR1
    status: pending
  - id: ex2-7
    content: Usar pthread_sigmask(SIG_BLOCK) em threads do jogo (pacman, ghost, board_update) para bloquear SIGUSR1
    status: pending
  - id: ex2-8
    content: Na thread anfitriã, verificar periodicamente se sigusr1_received == 1
    status: pending
  - id: ex2-9
    content: Criar função generate_log_file() que gera ficheiro com top 5 clientes
    status: pending
  - id: ex2-10
    content: Coletar pontuações de todas sessões ativas (sessions[i].board->pacmans[0].points)
    status: pending
  - id: ex2-11
    content: Ordenar clientes por pontuação (maior para menor) e selecionar top 5
    status: pending
  - id: ex2-12
    content: Escrever ficheiro de log com client_id e accumulated_points dos top 5
    status: pending
  - id: ex2-13
    content: Resetar flag sigusr1_received após gerar log
    status: pending
---

# Exercício 2 - Signal Handler SIGUSR1 e Log de Top 5 Clientes

## Estruturas e Variáveis Globais

### 1. Adicionar client_id à session_t

- **Arquivo**: `include/server.h` ou `include/threads.h`
- Adicionar campo `int client_id` à estrutura `session_t`
- Este ID identifica o cliente (extraído do `req_pipe_path` ou passado como parâmetro)
- Exemplo: se `req_pipe_path = "/tmp/1_request"`, então `client_id = 1`

### 2. Criar flag global para SIGUSR1

- **Arquivo**: `src/server/game.c` (variáveis globais/estáticas)
- Criar `volatile int sigusr1_received = 0;`
- Flag `volatile` para garantir que mudanças são visíveis entre threads
- Handler apenas define esta flag (não faz I/O pesado)

## Signal Handler

### 3. Criar função sigusr1_handler()

- **Arquivo**: `src/server/game.c`
- Assinatura: `void sigusr1_handler(int sig)`
- Apenas define `sigusr1_received = 1;`
- Não faz I/O, não chama funções não-async-signal-safe
- Parâmetro `sig` pode ser ignorado (sempre será SIGUSR1)

### 4. Registar signal handler no main()

- **Arquivo**: `src/server/game.c` (função `main()`)
- Incluir `#include <signal.h>`
- Usar `signal(SIGUSR1, sigusr1_handler)` ou `sigaction()`
- Registar antes de criar threads
- Verificar se registo foi bem-sucedido

## Bloqueio de SIGUSR1 nas Threads

### 5. Bloquear SIGUSR1 na thread anfitriã (se necessário)

- **Arquivo**: `src/server/game.c` (função `host_thread_func()`)
- Se a thread anfitriã for criada com `pthread_create()`, pode precisar de configurar máscara de sinais
- Se for a main thread, já recebe sinais por padrão
- Verificar qual thread realmente recebe o sinal

### 6. Bloquear SIGUSR1 nas threads gestoras

- **Arquivo**: `src/server/game.c` (função `session_manager_thread_func()`)
- No início da função, antes de qualquer operação:
- `sigset_t set;`
- `sigemptyset(&set);`
- `sigaddset(&set, SIGUSR1);`
- `pthread_sigmask(SIG_BLOCK, &set, NULL);`
- Isto garante que apenas thread anfitriã recebe SIGUSR1

### 7. Bloquear SIGUSR1 nas threads do jogo

- **Arquivo**: `src/server/game.c`
- Aplicar `pthread_sigmask(SIG_BLOCK, &set, NULL)` em:
- `pacman_thread_func()` (no início)
- `ghost_thread_func()` (no início)
- `board_update_thread_func()` (no início)
- Todas as threads exceto anfitriã devem bloquear SIGUSR1

## Geração de Log

### 8. Verificar flag na thread anfitriã

- **Arquivo**: `src/server/game.c` (função `host_thread_func()`)
- No loop principal, verificar periodicamente:
- `if (sigusr1_received == 1) { ... }`
- Quando flag estiver ativa, chamar função de geração de log
- Resetar flag após gerar log

### 9. Criar função generate_log_file()

- **Arquivo**: `src/server/game.c`
- Assinatura: `void generate_log_file(session_t sessions[], int max_games)`
- Coletar informações de todas sessões ativas
- Ordenar por pontuação
- Escrever ficheiro de log

### 10. Coletar pontuações de todas sessões

- **Arquivo**: `src/server/game.c` (dentro de `generate_log_file()`)
- Iterar sobre `sessions[0]` a `sessions[max_games-1]`
- Para cada sessão ativa (`session.active == 1`):
- Obter `client_id` de `session.client_id`
- Obter `accumulated_points` de `session.board->pacmans[0].points`
- Guardar em array temporário para ordenação

### 11. Ordenar clientes por pontuação

- **Arquivo**: `src/server/game.c` (dentro de `generate_log_file()`)
- Criar estrutura auxiliar: `typedef struct { int client_id; int points; } client_score_t;`
- Criar array `client_score_t scores[max_games]`
- Preencher com dados das sessões ativas
- Usar `qsort()` para ordenar por `points` (ordem decrescente)
- Função de comparação: `int compare_scores(const void* a, const void* b)`

### 12. Escrever ficheiro de log

- **Arquivo**: `src/server/game.c` (dentro de `generate_log_file()`)
- Criar/abrir ficheiro (ex: `"server_log.txt"` ou nome configurável)
- Escrever cabeçalho (opcional)
- Iterar sobre top 5 (ou menos se houver menos de 5 sessões ativas)
- Para cada cliente: escrever `client_id` e `accumulated_points`
- Formato sugerido:

```
=== Top 5 Clientes por Pontuação ===
1. Cliente ID: X, Pontuação: Y
2. Cliente ID: X, Pontuação: Y
...
```

- Fechar ficheiro

### 13. Resetar flag após gerar log

- **Arquivo**: `src/server/game.c` (dentro de `host_thread_func()`)
- Após chamar `generate_log_file()`, fazer `sigusr1_received = 0;`
- Permite processar novos sinais SIGUSR1

## Extração de client_id

### 14. Extrair client_id do req_pipe_path

- **Arquivo**: `src/server/game.c` (quando processar CONNECT)
- Se `req_pipe_path = "/tmp/1_request"`, extrair `1`
- Usar `sscanf()` ou parsing manual
- Exemplo: `sscanf(req_pipe_path, "/tmp/%d_request", &client_id)`
- Guardar em `session.client_id`

## Thread Safety

### 15. Proteger acesso às sessões ao gerar log

- **Arquivo**: `src/server/game.c` (função `generate_log_file()`)
- Usar mutex para proteger acesso a `sessions[]` durante leitura
- Ou usar `pthread_rwlock_rdlock()` se disponível
- Garantir que não há race conditions ao ler pontuações

## Notas de Implementação

- Handler deve ser simples e rápido (apenas definir flag)
- Não fazer I/O no handler (não é async-signal-safe)
- Geração de log deve ser feita na thread anfitriã, não no handler
- Se houver menos de 5 sessões ativas, listar apenas as existentes
- Se houver empate em pontuação, ordem é indiferente
- Ficheiro de log pode ser sobrescrito a cada SIGUSR1 ou usar timestamp
- Considerar usar mutex ao aceder `sigusr1_received` se necessário (mas `volatile` pode ser suficiente)
