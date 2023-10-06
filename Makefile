SRC =	
OBJ =	$(SRC:.c=.o)
BIN =	reallywhy

CC ?= clang
CFLAGS = -Wall -Wextra


all: bin

bin: $(OBJ)
	$(CC) $(OBJ) -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN) $(OBJ)
