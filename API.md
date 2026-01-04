---
name: Implementar Cliente API - Divisão Detalhada
overview: Dividir a implementação das 4 funções da API do cliente (pacman_connect, pacman_play, receive_board_update, pacman_disconnect) em tópicos específicos e testáveis.
todos: []
---

# Implementação da API do C

liente - Divisão em Tópicos

## Função 1: `pacman_connect()` - Estabelecer Conexão

### 1.1 Preparação dos FIFOs

- Criar FIFO de pedidos usando `mkfifo()` com `req_pipe_path`
- Criar FIFO de notificações usando `mkfifo()` com `notif_pipe_path`
- Verificar erros de criação (retornar 1 se falhar)
- Guardar os paths na estrutura `session`

### 1.2 Abrir FIFO de registo do servidor

- Abrir `server_pipe_path` em modo escrita (`O_WRONLY`)
- Verificar se a abertura foi bem-sucedida
- Guardar file descriptor (se necessário)

### 1.3 Preparar mensagem CONNECT

- Criar buffer para mensagem: `OP_CODE=1` (1 byte) + `req_pipe_path[40]` + `notif_pipe_path[40]`
- Preencher `req_pipe_path` com zeros até 40 caracteres (padding com `\0`)
- Preencher `notif_pipe_path` com zeros até 40 caracteres
- Usar `strncpy()` e `memset()` para garantir tamanho fixo

### 1.4 Enviar mensagem CONNECT

- Escrever mensagem completa (1 + 40 + 40 = 81 bytes) no pipe de registo
- Verificar se `write()` escreveu todos os bytes
- Fechar pipe de registo após envio

### 1.5 Abrir FIFOs do cliente

- Abrir `req_pipe_path` em modo leitura (`O_RDONLY`) para notificações
- Abrir `notif_pipe_path` em modo leitura (`O_RDONLY`) para notificações
- Guardar file descriptors em `session.req_pipe` e `session.notif_pipe`

### 1.6 Receber resposta do servidor

- Ler do pipe de notificações: `OP_CODE=1` (1 byte) + `result` (1 byte)
- Verificar se `read()` leu 2 bytes corretamente
- Verificar se `result == 0` (sucesso)
- Retornar 0 se sucesso, 1 se erro

### 1.7 Tratamento de erros

- Se qualquer passo falhar, limpar FIFOs criados com `unlink()`
- Fechar pipes abertos
- Retornar 1 em caso de erro

---

## Função 2: `pacman_play()` - Enviar Comando

### 2.1 Verificar sessão ativa

- Verificar se `session.req_pipe` está válido (>= 0)
- Se não houver sessão, retornar silenciosamente (ou logar erro)

### 2.2 Preparar mensagem PLAY

- Criar buffer: `OP_CODE=3` (1 byte) + `command` (1 byte)
- Total: 2 bytes

### 2.3 Enviar mensagem

- Escrever 2 bytes no `session.req_pipe`
- Verificar se `write()` escreveu ambos os bytes
- Não esperar resposta (função não bloqueante)

### 2.4 Tratamento de erros

- Se `write()` falhar (retorna -1 ou < 2), logar erro com `debug()`
- Não retornar erro (função é `void`)

---

## Função 3: `receive_board_update()` - Receber Atualização do Board

### 3.1 Verificar sessão ativa

- Verificar se `session.notif_pipe` está válido
- Se não houver sessão, retornar Board vazio

### 3.2 Ler cabeçalho da mensagem

- Ler `OP_CODE=4` (1 byte) - verificar se é realmente OP_CODE_BOARD
- Ler `width` (4 bytes, `int`)
- Ler `height` (4 bytes, `int`)
- Ler `tempo` (4 bytes, `int`)
- Ler `victory` (4 bytes, `int`)
- Ler `game_over` (4 bytes, `int`)
- Ler `accumulated_points` (4 bytes, `int`)
- Total cabeçalho: 1 + 6*4 = 25 bytes

### 3.3 Verificar leitura do cabeçalho

- Verificar se `read()` leu todos os 25 bytes
- Se falhar, retornar Board vazio (ou com `game_over=1`)

### 3.4 Alocar memória para board data

- Calcular tamanho: `width * height` bytes
- Alocar `char* data` com `malloc(width * height)`
- Verificar se alocação foi bem-sucedida

### 3.5 Ler dados do board

- Ler `width * height` bytes do pipe de notificações
- Verificar se leu todos os bytes corretamente
- Se falhar, libertar memória e retornar Board vazio

### 3.6 Construir estrutura Board

- Preencher `Board` com todos os valores lidos
- Atribuir `data` alocado
- Retornar estrutura `Board` completa

### 3.7 Tratamento de erros

- Se qualquer leitura falhar, libertar memória alocada
- Retornar Board com `game_over=1` e `data=NULL` para indicar erro
- Verificar se pipe foi fechado (read retorna 0) - indicar desconexão

---

## Função 4: `pacman_disconnect()` - Desconectar

### 4.1 Verificar sessão ativa

- Verificar se `session.req_pipe` está válido
- Se não houver sessão, retornar 0 (já desconectado)

### 4.2 Enviar mensagem DISCONNECT

- Preparar mensagem: `OP_CODE=2` (1 byte)
- Escrever no `session.req_pipe`
- Não esperar resposta (cliente fecha pipes imediatamente)

### 4.3 Fechar pipes

- Fechar `session.req_pipe` com `close()`
- Fechar `session.notif_pipe` com `close()`
- Verificar erros de fecho

### 4.4 Apagar FIFOs

- Apagar FIFO de pedidos: `unlink(session.req_pipe_path)`
- Apagar FIFO de notificações: `unlink(session.notif_pipe_path)`
- Verificar erros (mas não falhar se já não existirem)

### 4.5 Limpar estrutura session

- Resetar `session.id = -1`
- Resetar file descriptors para -1
- Limpar paths (opcional)

### 4.6 Retornar resultado

- Retornar 0 se tudo correu bem
- Retornar 1 apenas se houver erro crítico

---

## Ordem de Implementação Recomendada

1. **`pacman_play()`** - Mais simples (2 bytes, sem resposta)
2. **`pacman_disconnect()`** - Simples, mas precisa de `pacman_connect()` primeiro
3. **`receive_board_update()`** - Média complexidade (deserialização)