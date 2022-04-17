#
# simple Makefile template :)
#
Target=splayer

sample_dir=samples

decoder_src=$(wildcard ffdecoder/*.cpp)
decoder_objs:=$(patsubst %.cpp,%.o,$(decoder_src)) 

main_src:=$(wildcard src/*.cpp)
main_objs:=$(patsubst %.cpp,%.o,$(main_src)) 
 
picked_src:=$(wildcard src/*.c)
picked_objs:=$(patsubst %.c,%.o,$(picked_src))

util_src=$(wildcard utils/*.cpp)
util_objs:=$(patsubst %.cpp,%.o,$(util_src)) 


Objs:= $(decoder_objs) $(main_objs) $(util_objs) $(picked_objs)

#      以下摘自 `info make`
#
#Compiling C programs
# `N.o' is made automatically from `N.c' with a recipe of the form
#  `$(CC) $(CPPFLAGS) $(CFLAGS) -c'.
#
#Compiling C++ programs
#  `N.o' is made automatically from `N.cc', `N.cpp', or `N.C' with a recipe of the form 
#  `$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c'.  
#  We encourage you to use the suffix `.cc' for C++ source files insteadof `.C'.
#
#Linking a single object file
#  `N' is made automatically from `N.o' by running the linker
#  (usually called `ld') via the C compiler.  The precise recipe used
#  is `$(CC) $(LDFLAGS) N.o $(LOADLIBES) $(LDLIBS)'.

CC = gcc
CXX= g++

FFCFLAGS:=-I $(HOME)/ffmpeg_install_root/include/ \
		$(shell pkg-config --cflags sdl2)

FFLDFLAGS:=-L $(HOME)/ffmpeg_install_root/lib/ -pthread \
		  -lavdevice -lavfilter -lavformat -lavcodec -lswresample -lswscale -lavutil  \
		  $(shell pkg-config --libs sdl2) \
		  -lxcb -lxcb-shm -lxcb-shape -lxcb-xfixes -lxcb-render -lasound -lGL -lpulse \
		  -lm -llzma -L/usr/lib64 -lz

CPPFLAGS:= -DDEBUG -I. -Imin_ffmpeg  $(FFCFLAGS) 

CommonCC:= -ggdb3 -Wall -MMD  -pthread -fPIC 
CFLAGS:= $(CommonCC) -std=c11
CXXFLAGS:= $(CommonCC) -std=c++11

LDFLAGS= $(FFLDFLAGS) 
LOADLIBES=
LDLIBS= 


##########################################################################

Deps= $(Objs:.o=.d) 

all:$(Target)

-include $(Deps)

$(Target): $(Objs)
	$(CXX) $^ $(LDFLAGS)  $(LOADLIBES) $(LDLIBS) -o $@


clean:
	rm -fr $(Objs) $(Target) $(Deps)

test:$(Target) 
	./$(Target) chopin_revolution.mp4

valgrind:$(Target) 
	valgrind --leak-check=full ./$(Target) chopin_revolution.mp4

