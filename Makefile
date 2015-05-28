CC = /soft/compilers/bgclang/wbin/bgclang
CFLAGS = -O3 -g

UNW_HOME = ${HOME}/install/libunwind
CPPFLAGS = -I$(UNW_HOME)/include
LDFLAGS = -L$(UNW_HOME)/lib64 -Wl,-rpath,$(UNW_HOME)/lib64 -lunwind -lpthread

all: memlog.so memlog.a

memlog.a: memlog.c
	rm -f memlog.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o memlog.o memlog.c
	ar cr memlog.a memlog.o

memlog.so: memlog.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -fPIC -shared -o memlog.so memlog.c

clean:
	rm -f memlog.o memlog.a memlog.so

