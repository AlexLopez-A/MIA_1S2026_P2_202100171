CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I../include
LDFLAGS = -lpthread

SRC = src/main.cpp
TARGET = server

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) src/*.h include/*.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
