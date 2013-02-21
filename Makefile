
all:	mazers-n-lasers

snis_alloc.o:	snis_alloc.c snis_alloc.h
	$(CC) -c snis_alloc.c

joystick.o:	joystick.c joystick.h compat.h
	$(CC) -c joystick.c

mazers-n-lasers:	mazers-n-lasers.c joystick.o snis_alloc.o
	$(CC) -g -W -Wall -L. -o mazers-n-lasers \
		joystick.o \
		snis_alloc.o \
		mazers-n-lasers.c \
		-lopenlase

clean:
	rm -f mazers-n-lasers *.o
