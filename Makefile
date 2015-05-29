CXX = /soft/compilers/bgclang/wbin/bgclang++
CXXFLAGS = -std=gnu++11 -O3 -g

CPPFLAGS =
LDFLAGS = -lpthread -ldl

all: libmemlog.so memlog_s.o

memlog_s.o: memlog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o memlog_s.o memlog.cpp

libmemlog.so: memlog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -fPIC -shared -o libmemlog.so memlog.cpp

clean:
	rm -f memlog_s.o libmemlog.so

