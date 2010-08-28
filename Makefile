# SteelSeries XAI Mouse configuration tool makefile
# No autotools for one file..

CC=gcc
CFLAGS=-Wall

LIBS=-lusb

all: xaictl.c
	$(CC) $(CFLAGS) $? $(LIBS) -o xaictl
