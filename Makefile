# SteelSeries XAI Mouse configuration tool makefile
# No autotools for one file..

CC=gcc
CFLAGS=-Wall

LIBS=-lusb-1.0

all: xaictl.c
	$(CC) $(CFLAGS) $? $(LIBS) -o xaictl
