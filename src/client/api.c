#include "api.h"
#include "protocol.h"
#include "debug.h"

#include <errno.h> 
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>


struct Session {
  int id;
  int req_pipe;
  int notif_pipe;
  char req_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
  char notif_pipe_path[MAX_PIPE_PATH_LENGTH + 1];
};

static struct Session session __attribute__((unused)) = {.id = -1};

int pacman_connect(char const *req_pipe_path, char const *notif_pipe_path, char const *server_pipe_path) {

  if (unlink(req_pipe_path) != 0 && errno != ENOENT) {
    perror("[ERR]: unlink(req_pipe_path) failed");
    return 1;
  }

  if (mkfifo(req_pipe_path, 0640) != 0) {
    perror("[ERR]: mkfifo(req_pipe_path) failed");
    return 1;
  } 
  // Remover FIFO de notificações se já existir
  if (unlink(notif_pipe_path) != 0 && errno != ENOENT) {
    perror("[ERR]: unlink(notif_pipe_path) failed");
    return 1;
  }

  // Criar FIFO de notificações
  if (mkfifo(notif_pipe_path, 0640) != 0) {
    perror("[ERR]: mkfifo(notif_pipe_path) failed");
    return 1;
  }

   // Guardar paths na struct Session
  strncpy(session.req_pipe_path, req_pipe_path, MAX_PIPE_PATH_LENGTH);
  session.req_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';
  
  strncpy(session.notif_pipe_path, notif_pipe_path, MAX_PIPE_PATH_LENGTH);
  session.notif_pipe_path[MAX_PIPE_PATH_LENGTH] = '\0';

  int server_fd = open(server_pipe_path, O_WRONLY);
  if (server_fd == -1) {
    perror("[ERR]: open(server_pipe_path) failed");
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }
  // Preparar mensagem CONNECT
  // OP_CODE=1 (1 byte) + req_pipe_path[40] + notif_pipe_path[40] = 81 bytes
  char msg[81];
  msg[0] = OP_CODE_CONNECT;

  // Preencher req_pipe_path (40 bytes, padding com \0)
  memset(msg + 1, 0, 40);
  strncpy(msg + 1, req_pipe_path, MAX_PIPE_PATH_LENGTH);

  // Preencher notif_pipe_path (40 bytes, padding com \0)
  memset(msg + 41, 0, 40);
  strncpy(msg + 41, notif_pipe_path, MAX_PIPE_PATH_LENGTH);

  ssize_t bytes_written = 0;
  ssize_t total_bytes = 81;

  while (bytes_written < total_bytes) {
    ssize_t n = write(server_fd, msg + bytes_written, total_bytes - bytes_written);
    if (n == -1) {
      perror("[ERR]: write(server_pipe_path) failed");
      close(server_fd);
      unlink(req_pipe_path);
      unlink(notif_pipe_path);
      return 1;
    }
    bytes_written += n;
  }

  close(server_fd);

  session.req_pipe = open(req_pipe_path, O_RDONLY);
  if (session.req_pipe == -1) {
      perror("[ERR]: open(req_pipe_path) failed");
      unlink(req_pipe_path);
      unlink(notif_pipe_path);
    return 1;
  }

  session.notif_pipe = open(notif_pipe_path, O_RDONLY);
  if (session.notif_pipe == -1) {
    perror("[ERR]: open(notif_pipe_path) failed");
    close(session.req_pipe);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  // Receber resposta do servidor
  char response[2];
  ssize_t bytes_read = 0;
  ssize_t total_response_bytes = 2;

  while (bytes_read < total_response_bytes) {
    ssize_t n = read(session.notif_pipe, response + bytes_read, total_response_bytes - bytes_read);
    if (n == -1) {
      perror("[ERR]: read(notif_pipe) failed");
      close(session.req_pipe);
      close(session.notif_pipe);
      unlink(req_pipe_path);
      unlink(notif_pipe_path);
      return 1;
    }
    if (n == 0) {
      // Pipe fechado antes de receber resposta
      perror("[ERR]: server closed connection before sending response");
      close(session.req_pipe);
      close(session.notif_pipe);
      unlink(req_pipe_path);
      unlink(notif_pipe_path);
      return 1;
    }
    bytes_read += n;
  }

  // Verificar resposta
  if (response[0] != OP_CODE_CONNECT) {
    perror("[ERR]: invalid OP_CODE in server response");
    close(session.req_pipe);
    close(session.notif_pipe);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  if (response[1] != 0) {
    perror("[ERR]: server rejected connection");
    close(session.req_pipe);
    close(session.notif_pipe);
    unlink(req_pipe_path);
    unlink(notif_pipe_path);
    return 1;
  }

  // Sucesso - guardar ID da sessão (pode ser usado mais tarde)
  session.id = 0;

  (void)server_pipe_path;
  return 0;
}

void pacman_play(char command) {
  // TODO - implement me
  (void)command;
}

int pacman_disconnect() {
  // TODO - implement me
  return 0;
}

Board receive_board_update(void) {
    // TODO - implement me
  Board board = {0};
  return board;
}