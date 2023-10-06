CC=clang
CSRC=chash.c
COBJ=$(SRC,.o=.h)
CHEADERS=chash.h fatal.h
CFLAGS=-Wall -Wextra

%.o: %.c $(CHEADERS)
	$(CC) $(CFLAGS) -c $< -o $@


