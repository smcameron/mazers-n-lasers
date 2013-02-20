
all:	mazers-n-lasers

joystick.o:	joystick.c joystick.h compat.h
	$(CC) -c joystick.c

mazers-n-lasers:	mazers-n-lasers.c joystick.o
	$(CC) -g -W -Wall -L. -o mazers-n-lasers \
		joystick.o \
		mazers-n-lasers.c \
		-lopenlase

clean:
	rm -f mazers-n-lasers *.o
