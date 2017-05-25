all: src/main.cpp
	g++ -std=c++14 -Wall -Wextra src/main.cpp -o bin/chip8-interpreter -lSDL2


all2: src/chip8.cc
	g++ -std=c++14 -Wall -Wextra src/chip8.cc -o bin/chip8-interpreter2 -lSDL2

run:
	./bin/chip8-interpreter
