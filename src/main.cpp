#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <array>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <unordered_map>

#include <SDL2/SDL.h>

namespace chip8
{
    using Font = std::array<unsigned, 16>;
    namespace fonts
    {
        constexpr Font original_chip8
        {
            {
                0xF999F, 0x26227, 0xF1F8F, 0xF1F1F, 0x99F11, 0xF8F1F, 0xF8F9F, 0xF1244,
                0xF9F9F, 0xF9F1F, 0xF9F99, 0xE9E9E, 0xF888F, 0xE999E, 0xF8F8F, 0xF8F88
            }
        };
    }

    class Interpreter
    {
        union
        {
            unsigned char mem[4096]{};
            struct
            {
                unsigned char display[64 * 32 / 8], font[16 * 5], Vs[16], keys[16];
                unsigned char delay_timer, sound_timer, SP, wait_key;
                unsigned short stack[16], PC, I;
            } interp_data;
        };

    public:
        void copy_font(const Font& font) noexcept
        {
            unsigned char* fp = interp_data.font;
            for (unsigned c : font)
                for (int i = 16; i >= 0; i -= 4) *fp++ = c >> i & 0xF;
        }

        void copy_rom(const unsigned char* rom, unsigned size, unsigned loc = 0x200) noexcept
        {
            std::copy_n(rom, size, mem + loc);
            interp_data.PC = loc;
        }

        void blank_memory() noexcept {std::fill_n(mem, sizeof mem, 0);}

        void update_timers() noexcept
        {
            if (interp_data.delay_timer > 0) --interp_data.delay_timer;
            if (interp_data.sound_timer > 0) --interp_data.sound_timer;
        }

        void update_key(int code, bool status) noexcept {interp_data.keys[code] = status;}

        void set_wait_key(int code) noexcept
        {
            interp_data.Vs[interp_data.wait_key] = code;
            interp_data.wait_key = 0;
        }

        void execute_instruction() noexcept
        {
            const unsigned opcode = mem[interp_data.PC] << 8 | mem[interp_data.PC + 1];
            interp_data.PC += 2;

            const unsigned nnn = opcode & 0xFFF;
            const unsigned n   = opcode & 0xF;
            const unsigned kk  = opcode & 0xFF;
            
            const unsigned x = opcode >> 8  & 0xF;
            const unsigned y = opcode >> 4  & 0xF;
            const unsigned u = opcode >> 12 & 0xF;

            switch (u)
            {
                case 0x0:
                {
                    switch (nnn)
                    {
                        case 0x0E0: // CLS - clear the display
                            std::fill_n(interp_data.display, sizeof interp_data.display, 0);
                            break;
                        case 0x0EE: // RET - return from subroutine
                            interp_data.PC = interp_data.stack[interp_data.SP--];
                            break;
                    }
                    break;
                }
                case 0x1: // JP addr - jump to location nnn
                    interp_data.PC = nnn;
                    break;
                case 0x2: // CALL addr - call subroutine at nnn
                    interp_data.stack[++interp_data.SP] = interp_data.PC;
                    interp_data.PC = nnn;
                    break;
                case 0x3: // SE Vx, byte - skip next instruction if Vx = kk
                    if (interp_data.Vs[x] == kk)
                        interp_data.PC += 2;
                    break;
                case 0x4: // SNE Vx, byte - skip next instruction if Vx != kk
                    if (interp_data.Vs[x] != kk)
                        interp_data.PC += 2;
                    break;
                case 0x5: // SE Vx, Vy - skip next instruction if Vx = Vy 
                    if (!n)
                        if (interp_data.Vs[x] == interp_data.Vs[y])
                            interp_data.PC += 2;
                    break;
                case 0x6: // LD Vx, byte - set Vx = kk
                    interp_data.Vs[x] = kk;
                    break;
                case 0x7: // ADD Vx, byte - set Vx = Vx + kk
                    interp_data.Vs[x] += kk;
                    break;
                case 0x8:
                {
                    switch (n)
                    {
                        case 0x0: // LD Vx, Vy - set Vx = Vy
                            interp_data.Vs[x]  = interp_data.Vs[y];
                            break;
                        case 0x1: // OR Vx, Vy - set Vx = Vx OR Vy
                            interp_data.Vs[x] |= interp_data.Vs[y];
                            break;
                        case 0x2: // AND Vx, Vy - set Vx = Vx AND Vy
                            interp_data.Vs[x] &= interp_data.Vs[y];
                            break;
                        case 0x3: // XOR Vx, Vy - set Vx = Vx XOR Vy
                            interp_data.Vs[x] ^= interp_data.Vs[y];
                            break;
                        case 0x4: // ADD Vx, Vy - set Vx = Vx + Vy, set VF = carry
                        {
                            const unsigned temp = interp_data.Vs[x] + interp_data.Vs[y];
                            interp_data.Vs[0xF] = temp >> 8;
                            interp_data.Vs[  x] = temp;
                            break;
                        }
                        case 0x5: // SUB Vx, Vy - set Vx - Vy, set VF = NOT borrow
                        {
                            const unsigned temp = interp_data.Vs[x] - interp_data.Vs[y];
                            interp_data.Vs[0xF] = !(temp >> 8);
                            interp_data.Vs[  x] =   temp;
                            break;
                        }
                        case 0x6: // SHR Vx {, Vy} - set Vx = Vy SHR 1
                        {
                            interp_data.Vs[0xF] = interp_data.Vs[x] & 1; //
                            interp_data.Vs[  x] >>= 1;                   // On the original interpreter, the value of
                            break;                                       //
                        }                                                // Vy is shifted. On current implementations,
                        case 0xE: // SHL Vx {, Vy} - set Vx = Vy SHL 1   //
                        {                                                // Y is ignored.
                            interp_data.Vs[0xF] = interp_data.Vs[x] >> 7;//
                            interp_data.Vs[  x] <<= 1;     /*https://en.wikipedia.org/wiki/CHIP-8#cite_note-shift-2*/
                            break;                                       
                        }
                        case 0x7: // SUBN Vx, Vy - set Vx = Vy - Vx, set VF = NOT borrow
                        {
                            const unsigned temp = interp_data.Vs[y] - interp_data.Vs[x];
                            interp_data.Vs[0xF] = !(temp >> 8);
                            interp_data.Vs[  x] =   temp; 
                            break;
                        }
                    }
                    break;
                }
                case 0x9:
                    if (!n) // SNE Vx, Vy - skip next instruction if Vx != Vy 
                        if (interp_data.Vs[x] != interp_data.Vs[y])
                            interp_data.PC += 2;
                    break;
                case 0xA: // LD I, addr - set I = nnn
                    interp_data.I = nnn;
                    break;
                case 0xB: // JP V0, addr - jump to location nnn + V0
                    interp_data.PC = nnn + *interp_data.Vs;
                    break;
                case 0xC: // RND Vx, byte - set Vx = random byte AND kk
                    interp_data.Vs[x] = std::rand() % 256 & kk;
                    break;
                case 0xD: // DRW Vx, Vy nibble - display n-byte sprite starting at memory location I
                    {     // at (Vx, Vy), set VF = collision
                        const auto put = [this](int a, unsigned char b) noexcept {
                            return ((interp_data.display[a] ^= b) ^ b) & b;
                        }; 
                        const auto px = interp_data.Vs[x];
                        const auto py = interp_data.Vs[y];
                        unsigned collision = 0;
                        for(int i = n; i--;) 
                        {
                            collision |=
                                put(((px    ) % 64 + (py + i) % 32 * 64) / 8, mem[(interp_data.I + i)] >> (    px % 8)) |
                                put(((px + 8) % 64 + (py + i) % 32 * 64) / 8, mem[(interp_data.I + i)] << (8 - px % 8));
                        }
                        interp_data.Vs[0xF] = collision != 0;
                    }
                    break;
                case 0xE:
                {
                    switch (kk)
                    {
                        case 0x9E: // SKP Vx - skip next instruction if key with the value pf Vx is pressed
                            if (interp_data.keys[interp_data.Vs[x]])
                                interp_data.PC += 2;
                            break;
                        case 0xA1: // SKNP Vx - skip next instruction if key with the value of Vx is not pressed
                            if (!interp_data.keys[interp_data.Vs[x]])
                                interp_data.PC += 2;
                            break;
                    }
                    break;
                }
                case 0xF:
                {
                    switch (kk)
                    {
                        case 0x07: // LD Vx, DT - set Vx = dispaly timer value
                            interp_data.Vs[x] = interp_data.delay_timer;
                            break;
                        case 0x0A: // LD Vx, K - wait for a key press, store the value of the key in Vx
                            interp_data.wait_key = x;
                            break;
                        case 0x15: // LD DT, Vx - set delay timer = Vx
                            interp_data.delay_timer = interp_data.Vs[x];
                            break;
                        case 0x18: // LD ST, Vx - set sound timer = Vx
                            interp_data.sound_timer = interp_data.Vs[x];
                            break;
                        case 0x1E: // ADD I, Vx - set I = I + Vx
                        {
                            const unsigned  temp = interp_data.I + interp_data.Vs[x];
                            interp_data.Vs[0xF] = temp >> 12;
                            interp_data.I = temp;
                            // VF is set to 1 when there is a range overflow (I+VX>0xFFF), and to 0 when there isn't.
                            // This is an undocumented feature of the CHIP-8
                            //
                            // https://en.wikipedia.org/wiki/CHIP-8#cite_note-onlgame-3
                            break;
                        }
                        case 0x29: // LD F, Vx - set I = location of sprite for digit Vx
                            interp_data.I = interp_data.font + interp_data.Vs[x] * 5 - mem;
                            break;
                        case 0x33: // LD B, Vx - store BCD representation of Vx in memory locations I, I + 1, and I + 2
                        {
                            mem[interp_data.I + 2] = interp_data.Vs[x]       % 10;
                            mem[interp_data.I + 1] = interp_data.Vs[x] /  10 % 10;
                            mem[interp_data.I    ] = interp_data.Vs[x] / 100 % 10;
                            break;
                        }
                    /**/case 0x55: // LD [I], Vx - store registers V0 through Vx in memory starting at location I
                    /**/    //std::copy_n(interp_data.Vs, x + 1, mem + interp_data.I);
                    /**/    for (unsigned i = 0; i <= x; ++i)                
                    /**/        mem[interp_data.I++] = interp_data.Vs[i];
                    /**/    break;
                    /**/case 0x65: // LD Vx, [I] - read registers V0 through Vx from memory starting at location I
                    /**/    //std::copy_n(mem + interp_data.I, x + 1, interp_data.Vs);
                    /**/    for (unsigned i = 0; i <= x; ++i)
                    /**/        interp_data.Vs[i] = mem[interp_data.I++];
                    /**/    break;
                    /** On the original interpreter, when the operation is done, I=I+X+1.*/
                    /** On current implementations, I is left unchanged.
                     ** 
                     ** https://en.wikipedia.org/wiki/CHIP-8#cite_note-memi-4
                     **
                     ** Old version is needed for same programs though.
                     **
                     **/
                    }
                    break;
                }
            }
        }

        const unsigned char* display() const noexcept {return interp_data.display;}

        bool wait() const noexcept {return interp_data.wait_key;}
    };
}

namespace
{
    void blit_chip8_display(const chip8::Interpreter& interp, Uint32* buffer) noexcept
    {
        const unsigned char* const display = interp.display();
        for (int i = 0; i < 64 * 32; ++i)
            buffer[i] = 0xFFFFFFFF * (display[i >> 3] >> (7 - i % 8) & 1);
    }

    std::vector<unsigned char> load_binary_file(const std::string& filepath)
    {
        std::ifstream stream{filepath, std::ios::in | std::ios::binary};
        if (!stream)
            throw std::runtime_error{"file reading error: " + filepath};
        
        stream.seekg(0, std::ios::end);
        const auto size = stream.tellg();
        
        stream.seekg(0, std::ios::beg);

        std::vector<unsigned char> chars(size);
        stream.read(reinterpret_cast<char*>(chars.data()), size);
        
        return chars;
    }
}

int main()
{
    if (::SDL_Init(SDL_INIT_VIDEO) >= 0)
    {
        constexpr int WINDOW_WIDTH  = 640;
        constexpr int WINDOW_HEIGHT = 480;

        SDL_Window* const window = 
            ::SDL_CreateWindow("CHIP-8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                    WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
        if (window)
        {
            SDL_Renderer* const renderer = ::SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
            if (renderer)
            {
                SDL_Texture* const texture =
                    ::SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 64, 32);
                if (texture)
                {
                    chip8::Interpreter chip8_interpreter;

                    bool rom_loading_status = true;
                    try
                    {
                        std::vector<unsigned char> rom{::load_binary_file("res/HANOI")};
                        chip8_interpreter.copy_rom(rom.data(), rom.size());
                    }
                    catch (const std::exception& ex)
                    {
                        std::cerr << ex.what() << std::endl;
                        rom_loading_status = false;
                    }

                    if (rom_loading_status)
                    {
                        chip8_interpreter.copy_font(chip8::fonts::original_chip8);

                        const std::unordered_map<SDL_Scancode, int> keys_map
                        {
                            {SDL_SCANCODE_1, 0x1}, {SDL_SCANCODE_2, 0x2}, {SDL_SCANCODE_3, 0x3}, {SDL_SCANCODE_C, 0xC},
                            {SDL_SCANCODE_4, 0x4}, {SDL_SCANCODE_5, 0x5}, {SDL_SCANCODE_6, 0x6}, {SDL_SCANCODE_D, 0xD},
                            {SDL_SCANCODE_7, 0x7}, {SDL_SCANCODE_8, 0x8}, {SDL_SCANCODE_9, 0x9}, {SDL_SCANCODE_E, 0xE},
                            {SDL_SCANCODE_A, 0xA}, {SDL_SCANCODE_0, 0x0}, {SDL_SCANCODE_F, 0xB}, {SDL_SCANCODE_F, 0xF}
                        };
                        
                        constexpr unsigned insts_per_frame = 50000, timers_updating_period = 1000 / 60;
                        unsigned acc_time = 0;

                        for (bool running = true; running;)
                        {
                            const Uint32 start_time = ::SDL_GetTicks();

                            for (static SDL_Event event; ::SDL_PollEvent(&event);)
                            {
                                switch (event.type)
                                {
                                    case SDL_QUIT:
                                        running = false;
                                        break;
                                    case SDL_KEYDOWN:
                                        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                                            running = false;
                                    case SDL_KEYUP:
                                    {
                                        const auto key_iter = keys_map.find(event.key.keysym.scancode);
                                        if (key_iter != keys_map.cend())
                                        {
                                            chip8_interpreter.update_key(key_iter->second, event.type == SDL_KEYDOWN);
                                            if (chip8_interpreter.wait() && event.type == SDL_KEYDOWN)
                                                chip8_interpreter.set_wait_key(key_iter->second);
                                        }
                                        break;
                                    }
                                }
                            }

                            for (unsigned i = 0; i < insts_per_frame && !chip8_interpreter.wait(); ++i)
                                chip8_interpreter.execute_instruction();

                            Uint32* pixels;
                            int pitch;

                            ::SDL_LockTexture(texture, nullptr, reinterpret_cast<void**>(&pixels), &pitch);
                            ::blit_chip8_display(chip8_interpreter, pixels);
                            
                            ::SDL_UnlockTexture(texture);

                            ::SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                            
                            ::SDL_RenderPresent(renderer);

                            for (acc_time += ::SDL_GetTicks() - start_time; acc_time >= timers_updating_period;
                                                                            acc_time -= timers_updating_period)
                                chip8_interpreter.update_timers();
                        }
                    }

                    ::SDL_DestroyTexture(texture);
                }
                else
                    std::cerr << "SDL_Texture creation error: " << ::SDL_GetError() << std::endl;

                ::SDL_DestroyRenderer(renderer);
            }
            else
                std::cerr << "SDL_Renderer creation error: " << ::SDL_GetError() << std::endl;

            ::SDL_DestroyWindow(window);
        }
        else
            std::cerr << "SDL_Window creation error: " << ::SDL_GetError() << std::endl;

        ::SDL_Quit();
    }
    else
        std::cerr << "SDL2 initialization error: " << ::SDL_GetError() << std::endl;

    return 0;
}
