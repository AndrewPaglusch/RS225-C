CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -Isrc
LDFLAGS = -lm

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

TARGET = $(BIN_DIR)/rs225

SERVER_SRC = $(filter-out $(SRC_DIR)/gameshell.c $(SRC_DIR)/clientstream.c $(SRC_DIR)/custom.c $(SRC_DIR)/pixmap.c $(SRC_DIR)/platform.c $(SRC_DIR)/platform_server.c $(SRC_DIR)/inputtracking.c, $(wildcard $(SRC_DIR)/*.c))
SOURCES = $(SERVER_SRC) $(SRC_DIR)/platform_server.c $(wildcard $(SRC_DIR)/datastruct/*.c) $(filter-out $(SRC_DIR)/thirdparty/isaac.c, $(wildcard $(SRC_DIR)/thirdparty/*.c))
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR) $(OBJ_DIR)/datastruct $(OBJ_DIR)/thirdparty $(OBJ_DIR)/sound $(OBJ_DIR)/wordenc

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

run: $(TARGET)
	./$(TARGET)
