CC = i686-pc-toaru-gcc
CFLAGS = -std=c99
LIBS = `pkg-config --libs libavcodec libavutil libavformat libswscale` -ltoaru -lpng -lz -lm

.PHONY: all clean install

all: vidplayer

clean:
	-rm -f vidplayer

install: vidplayer
	cp vidplayer $(TOARU_SYSROOT)/usr/bin/

vidplayer: vidplayer.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)


