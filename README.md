# chip8-interpreter

A Chip8 interpreter and a compiler written in C++14.

An example of a program in a pseudo assembly language:
```
jp start

magic_sprite: byte "((", 0, "B", 0x3C

; v1 - x, v2 - y, v3 - count
draw_magic_sprite:
    ld I, magic_sprite
    ld v0, 1
    repeat:
        drw v1, v2, 5
        add v1, 9
        sub v3, v0
        se v3, 0
        jp repeat
    ret

start:
    ld v0, 0x6 ;ddaw
    ld F, v0   

    ld v0, 16
    ld v1, 8
    drw v0, v1, 5          

    ld v0, 21
    ld v1, 8
    drw v0, v1, 5

    ld v0, 26
    ld v1, 8
    drw v0, v1, 5

    ld v1, 10
    ld v2, 16
    ld v3, 3
    call draw_magic_sprite

    ld v0, 10
    ld DT, v0

    ld v0, 1
    ld ST, v0

wait:
    ld v0, DT
    sne v0, 0
    jp start
    jp wait

```

Here is a result after compiling the code above:

![alt tag](https://github.com/jangolare/chip8-interpreter/blob/master/res/example.png)


## Building

The project requires SDL2 library to be installed in your system.

**Enter the following commands to build for Linux**
```
cd project_root_directory
mkdir bin
make interpreter (or just make to build the interpreter)
make compiler
```

Then you can type `make run` in otder to test the program.



