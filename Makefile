interpreter: src/interpreter.cpp
	g++ -std=c++14 -pedantic -Wall -Wextra src/interpreter.cpp -o bin/chip8-interpreter -lSDL2

compiler: src/compiler.cpp
	g++ -std=c++14 -pedantic -Wall -Wextra src/compiler.cpp -o bin/chip8-compiler -lSDL2

run:
	./bin/chip8-compiler
	./bin/chip8-interpreter
