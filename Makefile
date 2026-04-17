# Linux Makefile (experimental — see README for status).
#
# The audio path in BBCSound.cpp / SampleManager.cpp currently uses macOS
# AudioToolbox unconditionally; until those are guarded or ported to a
# cross-platform backend the Linux build will fail at link time. The build
# up to the audio link errors is functional on Ubuntu 22.04+.

SOURCES := Bus.cpp Exile.cpp olc6502.cpp SampleManager.cpp BBCSound.cpp SN76489.cpp Main.cpp
OBJS    := $(SOURCES:.cpp=.o)

CXX := g++
CXXFLAGS := -std=c++17 -O3 -march=native -flto
LDFLAGS  := -lX11 -pthread -lpng -lGL -march=native -flto

exile: $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) exile
