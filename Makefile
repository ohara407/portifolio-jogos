CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

all: c/robo-coleta/robo_coleta c/labirinto-3d/labirinto3d

c/robo-coleta/robo_coleta: c/robo-coleta/robo_coleta.c
	$(CC) $(CFLAGS) -o $@ $<

c/labirinto-3d/labirinto3d: c/labirinto-3d/labirinto3d.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f c/robo-coleta/robo_coleta c/labirinto-3d/labirinto3d *.exe c/*/*.exe

.PHONY: all clean
