CC = /soft/compilers/bgclang/wbin/bgclang
CFLAGS = -std=gnu99 -O3 -g

CPPFLAGS =
LDFLAGS = -lpthread -ldl

all: libmemlog.so memlog_s.o

memlog_s.o: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o memlog_s.o memlog.c

libmemlog.so: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o libmemlog.so memlog.c

clean:
	rm -f memlog_s.o libmemlog.so

