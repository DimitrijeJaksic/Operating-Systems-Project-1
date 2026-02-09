# Compiler & flags
CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -g

# Source files
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
OUT = shell

# Build target
all: $(OUT)

$(OUT): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(OUT)

# Compile .c → .o
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJ) $(OUT)
