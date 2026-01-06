# Compiler variables
CC = gcc
CFLAGS = -I include -g -Wall -Wextra -Werror -std=c17 -D_POSIX_C_SOURCE=200809L
LDFLAGS_SERVER = -lncurses -lpthread
LDFLAGS_CLIENT = -lncurses -lpthread

# Directory variables
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# Server
SERVER_SRC_DIR = src/server
SERVER_TARGET = PacmanIST
SERVER_OBJS = game.o board.o threads.o display.o

# Client
CLIENT_SRC_DIR = src/client
CLIENT_TARGET = client
CLIENT_OBJS = client_main.o api.o display.o debug.o

# Object files path
vpath %.o $(OBJ_DIR)

# Make targets
all: server client

# ============ SERVER ============
server: $(BIN_DIR)/$(SERVER_TARGET)

$(BIN_DIR)/$(SERVER_TARGET): $(addprefix $(OBJ_DIR)/server_, $(SERVER_OBJS)) | folders
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SERVER)

$(OBJ_DIR)/server_%.o: $(SERVER_SRC_DIR)/%.c | folders
	$(CC) $(CFLAGS) -o $@ -c $<

# ============ CLIENT ============
client: $(BIN_DIR)/$(CLIENT_TARGET)

$(BIN_DIR)/$(CLIENT_TARGET): $(addprefix $(OBJ_DIR)/client_, $(CLIENT_OBJS)) | folders
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_CLIENT)

$(OBJ_DIR)/client_%.o: $(CLIENT_SRC_DIR)/%.c | folders
	$(CC) $(CFLAGS) -o $@ -c $<

# ============ RUN ============
# Run server: make run-server levels/ [max_games] [register_pipe]
DIR := $(word 2,$(MAKECMDGOALS))
MAX_GAMES ?= 3
REGISTER_PIPE ?= /tmp/pacman_register
run-server: server
	@if [ -z "$(DIR)" ]; then \
		echo "Usage: make run-server <levels_directory> [MAX_GAMES=N] [REGISTER_PIPE=path]"; \
		echo "Example: make run-server levels/ MAX_GAMES=3 REGISTER_PIPE=/tmp/pacman_register"; \
		exit 1; \
	fi
	@./$(BIN_DIR)/$(SERVER_TARGET) $(DIR) $(MAX_GAMES) $(REGISTER_PIPE)

# Run client: make run-client ID=1 PIPE=/tmp/pacman_register
run-client: client
	@if [ -z "$(ID)" ] || [ -z "$(PIPE)" ]; then \
		echo "Usage: make run-client ID=<client_id> PIPE=<register_pipe>"; \
		exit 1; \
	fi
	@./$(BIN_DIR)/$(CLIENT_TARGET) $(ID) $(PIPE)

# Prevent Make from trying to build the directory as a target
%:
	@:

# ============ UTILITIES ============
folders:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(BIN_DIR)

.PHONY: all clean folders server client run-server run-client
