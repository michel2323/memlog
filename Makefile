CC = /soft/compilers/bgclang/wbin/bgclang
CFLAGS = -O3 -g

CPPFLAGS =
LDFLAGS = -lpthread -ldl

all: memlog.so memlog.a

memlog.a: memlog.c
	rm -f memlog.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o memlog.o memlog.c
	ar cr memlog.a memlog.o

memlog.so: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o memlog.so memlog.c

clean:
	rm -f memlog.o memlog.a memlog.so

