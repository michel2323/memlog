# *****************************************************************************
#                   Copyright (C) 2015, UChicago Argonne, LLC
#                              All Rights Reserved
#                            memlog (ANL-SF-15-081)
#                    Hal Finkel, Argonne National Laboratory
# 
#                              OPEN SOURCE LICENSE
# 
# Under the terms of Contract No. DE-AC02-06CH11357 with UChicago Argonne, LLC,
# the U.S. Government retains certain rights in this software.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 
# 3. Neither the names of UChicago Argonne, LLC or the Department of Energy nor
#    the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission. 
#  
# *****************************************************************************
#                                  DISCLAIMER
# 
# THE SOFTWARE IS SUPPLIED “AS IS” WITHOUT WARRANTY OF ANY KIND.
# 
# NEITHER THE UNTED STATES GOVERNMENT, NOR THE UNITED STATES DEPARTMENT OF
# ENERGY, NOR UCHICAGO ARGONNE, LLC, NOR ANY OF THEIR EMPLOYEES, MAKES ANY
# WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LEGAL LIABILITY OR RESPONSIBILITY
# FOR THE ACCURACY, COMPLETENESS, OR USEFULNESS OF ANY INFORMATION, DATA,
# APPARATUS, PRODUCT, OR PROCESS DISCLOSED, OR REPRESENTS THAT ITS USE WOULD NOT
# INFRINGE PRIVATELY OWNED RIGHTS.
# 
# *****************************************************************************

CXX = /soft/compilers/bgclang/wbin/bgclang++
CXXFLAGS = -std=gnu++0x -O3 -g

# When compiling with CXX=powerpc64-bgq-linux-g++, we need these:
CPPFLAGS = -I/bgsys/drivers/ppcfloor -I/bgsys/drivers/ppcfloor/spi/include/kernel/cnk

LDFLAGS = -lpthread -ldl

# Set this to use the install target
DESTDIR = /dev/null

all: libmemlog.so memlog_s.o

memlog_s.o: memlog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o memlog_s.o memlog.cpp

libmemlog.so: memlog.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -fPIC -shared -o libmemlog.so memlog.cpp

install: all memlog_analyze README
	cp -a libmemlog.so memlog_s.o memlog_analyze README $(DESTDIR)/
	echo '-Wl,--wrap,malloc,--wrap,valloc,--wrap,realloc,--wrap,calloc,--wrap,memalign,--wrap,free,--wrap,posix_memalign,--wrap,mmap,--wrap,mmap64,--wrap,munmap $(DESTDIR)/memlog_s.o -lpthread -ldl' > $(DESTDIR)/memlog_s_ld_cmds

clean:
	rm -f memlog_s.o libmemlog.so

