CXX = /soft/compilers/bgclang/wbin/bgclang++
CXXFLAGS = -std=gnu++0x -O3 -g

CPPFLAGS =
LDFLAGS = -lpthread -ldl

# Set this to use the install target
DESTDIR = /dev/null

all: libmemlog.so memlog_s.o

memlog_s.o: memlog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o memlog_s.o memlog.cpp

libmemlog.so: memlog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -fPIC -shared -o libmemlog.so memlog.cpp

install: all memlog2dot README
	cp -a libmemlog.so memlog_s.o memlog2dot README $(DESTDIR)/

clean:
	rm -f memlog_s.o libmemlog.so

