
CXX	= g++
CPPFLAGS = -I../../ -Wall -g -Wextra -Werror -Wno-unused-parameter -fvisibility="hidden"
CXXFLAGS = -std=c++0x
LDFLAGS = -g -rdynamic -L/usr/local/bin
#LIBS = -lpthread

SRCS = $(wildcard *.cc)
TARGET = test

all: $(TARGET) 

$(TARGET): $(SRCS:.cc=.o)
	$(CXX) $(LDFLAGS) -o $@ $^

%.o: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.PHONY: clean

clean:
	rm -f *.o $(TARGET) core

