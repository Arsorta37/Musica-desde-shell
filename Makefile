CXX = g++
CXXFLAGS = -std=c++23 -O2 -Wall -Wextra
TARGET = Reproductor

# Detección automática de SO
ifeq ($(OS),Windows_NT)
    PLATFORM = Windows
    LIBS = -lole32 -lwinmm
    EXE = .exe
else
    PLATFORM = Linux
    LIBS = -lpthread -ldl -lm
    EXE =
endif

SRCS = main.cpp
HDRS = reproductor.hpp terminal.hpp lectorShell.hpp miniaudio.h

all: $(TARGET)$(EXE)

$(TARGET)$(EXE): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)$(EXE) $(LIBS)

clean:
ifeq ($(OS),Windows_NT)
	-del /Q /F $(TARGET)$(EXE) 2>nul
	-del /Q /F *.o 2>nul
else
	rm -f $(TARGET)$(EXE) *.o
endif

.PHONY: all clean