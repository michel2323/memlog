CC = /soft/compilers/bgclang/wbin/bgclang
CFLAGS = -std=gnu99 -O3 -g

CPPFLAGS =
LDFLAGS = -lpthread -ldl

all: libmemlog.so memlog.o

memlog.o: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o memlog.o memlog.c

libmemlog.so: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o libmemlog.so memlog.c

clean:
	rm -f memlog.o libmemlog.so

