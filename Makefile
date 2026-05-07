CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 \
            -I include \
            -D_POSIX_C_SOURCE=200809L
LDFLAGS := -lpthread

# Debug build adds sanitisers; Release turns on optimisation
DEBUG_FLAGS   := -g -O0 -fsanitize=address,undefined
RELEASE_FLAGS := -O2 -DNDEBUG

SRC_DIR := src
OBJ_DIR := build
BIN     := scheduler

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# ── Default: debug build ─────────────────────────────────────────────────────
.PHONY: all
all: CFLAGS += $(DEBUG_FLAGS)
all: LDFLAGS += $(DEBUG_FLAGS)
all: $(BIN)

# ── Release build ────────────────────────────────────────────────────────────
.PHONY: release
release: CFLAGS += $(RELEASE_FLAGS)
release: $(BIN)

# ── Link ─────────────────────────────────────────────────────────────────────
$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build complete → $(BIN)"

# ── Compile ──────────────────────────────────────────────────────────────────
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# ── Run ──────────────────────────────────────────────────────────────────────
.PHONY: run
run: all
	./$(BIN)

# ── Valgrind (memory leak check) ─────────────────────────────────────────────
.PHONY: valgrind
valgrind: CFLAGS := -Wall -Wextra -std=c11 -I include -D_POSIX_C_SOURCE=200809L -g -O0
valgrind: LDFLAGS := -lpthread
valgrind: $(BIN)
	valgrind --leak-check=full \
	         --track-origins=yes \
	         --show-leak-kinds=all \
	         --error-exitcode=1 \
	         ./$(BIN)

# ── Clean ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN)
	@echo "Cleaned."
