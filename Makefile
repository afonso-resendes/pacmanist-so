# Compiler variables
CC = gcc
CFLAGS = -g -Wall -Wextra -Werror -std=c17 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lncurses -lpthread

# Directory variables
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# executable 
TARGET = Pacmanist

# Objects variables
OBJS = game.o display.o board.o threads.o

# Dependencies
display.o = display.h
board.o = board.h
threads.o = threads.h

# Object files path
vpath %.o $(OBJ_DIR)
vpath %.c $(SRC_DIR)

# Make targets
all: pacmanist

pacmanist: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(OBJS) | folders
	$(CC) $(CFLAGS) $(SLEEP) $(addprefix $(OBJ_DIR)/,$(OBJS)) -o $@ $(LDFLAGS)

# dont include LDFLAGS in the end, to allow compilation on macos
%.o: %.c $($@) | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -o $(OBJ_DIR)/$@ -c $<

# run the program
# Usage: make run levels
#        make run test_levels
# Captura o primeiro argumento após "run"
DIR := $(word 2,$(MAKECMDGOALS))
run: pacmanist
	@if [ -z "$(DIR)" ]; then \
		echo "ERRO: Deves especificar o diretório: make run <diretório>"; \
		exit 1; \
	fi
	@./$(BIN_DIR)/$(TARGET) $(DIR)

# Prevenir que o Make tente construir o diretório como target
%:
	@:

# Create folders
folders:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

# Clean object files and executable
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f $(BIN_DIR)/$(TARGET)
	rm -f *.log

# indentify targets that do not create files
.PHONY: all clean run folders
