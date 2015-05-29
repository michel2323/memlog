CC = /soft/compilers/bgclang/wbin/bgclang
CFLAGS = -std=gnu99 -O3 -g

CPPFLAGS =
LDFLAGS = -lpthread -ldl

all: libmemlog.so libmemlog.a

libmemlog.a: memlog.c
	rm -f memlog.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o memlog.o memlog.c
	ar cr libmemlog.a memlog.o
	ranlib libmemlog.a

libmemlog.so: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o libmemlog.so memlog.c

clean:
	rm -f memlog.o libmemlog.a libmemlog.so

