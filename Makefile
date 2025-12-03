CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lrt

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = .

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TARGET = $(BIN_DIR)/server

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

# Limpar recursos IPC antigos (SHM/Sems) para evitar erros no arranque
run: $(TARGET)
	@echo "🧹 A limpar recursos IPC antigos (SHM e Semáforos)..."
	-rm -f /dev/shm/ws_* /dev/shm/sem.ws_* 2>/dev/null || true
	@echo "🚀 A iniciar o servidor..."
	./$(TARGET)

test: $(TARGET)
	cd tests && bash test_load.sh

.PHONY: all clean run test