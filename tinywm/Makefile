PREFIX?=/usr/X11R6
CFLAGS?=-Os -pedantic -Wall

all:
	$(CC) $(CFLAGS) -I$(PREFIX)/include shedwm.c -L$(PREFIX)/lib -lX11 -o shedwm

clean:
	rm -f shedwm

test: all
	xinit ./shedwm -- :1
