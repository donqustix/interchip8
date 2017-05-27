#include <unordered_map>
#include <utility>
#include <fstream>
#include <random>
#include <chrono>
#include <deque>

#include <SDL2/SDL.h>

/* Chip8 interpreter - Created by Joel Yliluoma
 * For video presentation at https://www.youtube.com/watch?v=rpLoS7B6T94
 */

static constexpr const unsigned W = 64, H = 32;

struct Chip8
{
    union
    {
        // The Chip-8 has 0x1000 bytes of RAM.
        unsigned char Mem[0x1000] {0x12,0x00}; // It begins with a "JP 0x200" instruction.
        // Within the beginning of the RAM, are the simulator internals (up to 0x200 bytes).
        struct
        {
            unsigned char V[16], DelayTimer, SoundTimer, SP, Keys[16], WaitingKey;
            unsigned char DispMem[W*H/8], Font[16*5]; // monochrome bitmaps
            unsigned short PC, Stack[12], I;
        };
    };

    // The CPU contains a random number device, for use with the RND instruction.
    std::mt19937 rnd{};

    // Initialization
    Chip8()
    {
        // Install the built-in font
        auto* p = Font;
        for(unsigned n: { 0xF999F,0x26227,0xF1F8F,0xF1F1F,0x99F11,0xF8F1F,0xF8F9F,0xF1244,
                          0xF9F9F,0xF9F1F,0xF9F99,0xE9E9E,0xF888F,0xE999E,0xF8F8F,0xF8F88 })
            { for(int a=16; a>=0; a-=4) *p++ = (n >> a) & 0xF; }
    }

    #define LIST_INSTRUCTIONS(o) \
        o("cls",         "00E0", u==0x0&& nnn==0xE0, for(auto& c: DispMem) c=0      ) /* clear display           */ \
        o("ret",         "00EE", u==0x0&& nnn==0xEE, PC = Stack[SP-- % 12]          ) /* set PC = stack[SP--]    */ \
        o("jp N",        "1nnn", u==0x1            , PC = nnn                       ) /* set PC = nnn            */ \
        o("jp v0,N",     "Bnnn", u==0xB            , PC = nnn + V[0]                ) /* set PC = nnn + V0       */ \
        o("call N",      "2nnn", u==0x2            , Stack[++SP % 12] = PC; PC = nnn) /* store stack[++SP]=PC, then PC=nnn */ \
        o("ld vX,k",     "Fx0A", u==0xF && kk==0x0A, WaitingKey = 0x80 | x          ) /* Wait for key press, set Vx = key value */ \
        o("ld vX,K",     "6xkk", u==0x6            , Vx = kk                        ) /* set Vx = kk             */ \
        o("ld vX,vY",    "8xy0", u==0x8 &&  p==0x0 , Vx = Vy                        ) /* set Vx = Vy             */ \
        o("add vX,K",    "7xkk", u==0x7            , Vx += kk                       ) /* set Vx = Vx + kk        */ \
        o("or vX,vY",    "8xy1", u==0x8 &&  p==0x1 , Vx |= Vy                       ) /* set Vx = Vx | Vy        */ \
        o("and vX,vY",   "8xy2", u==0x8 &&  p==0x2 , Vx &= Vy                       ) /* set Vx = Vx & Vy        */ \
        o("xor vX,vY",   "8xy3", u==0x8 &&  p==0x3 , Vx ^= Vy                       ) /* set Vx = Vx ^ Vy        */ \
        o("add vX,vY",   "8xy4", u==0x8 &&  p==0x4 , p = Vx+Vy; VF =  (p>>8); Vx = p) /* set Vx = Vx + Vy, update VF = carry */ \
        o("sub vX,vY",   "8xy5", u==0x8 &&  p==0x5 , p = Vx-Vy; VF = !(p>>8); Vx = p) /* set Vx = Vx - Vy, update VF = NOT borrow */ \
        o("subn vX,vY",  "8xy7", u==0x8 &&  p==0x7 , p = Vy-Vx; VF = !(p>>8); Vx = p) /* set Vx = Vy - Vx, update VF = NOT borrow */ \
        o("shr vX",      "8xy6", u==0x8 &&  p==0x6 , VF = Vy  & 1;  Vx = Vy >> 1    ) /* set Vx = Vy >> 1, update VF = carry */ \
        o("shl vX",      "8xyE", u==0x8 &&  p==0xE , VF = Vy >> 7;  Vx = Vy << 1    ) /* set Vx = Vy << 1, update VF = carry */ \
        o("rnd vX,K",    "Cxkk", u==0xC            , \
           Vx = std::uniform_int_distribution<>(0,255)(rnd) & kk) /* set Vx = random() & kk  */ \
        o("drw vX,vY,P", "Dxyl", u==0xD            , \
            auto put = [this](int a, unsigned char b) { return ((DispMem[a] ^= b) ^ b) & b; }; \
            for(kk=0, x=Vx, y=Vy; p--; ) \
                kk |= put( ((x+0)%W + (y+p)%H * W) / 8, Mem[(I+p)&0xFFF] >> (    x%8) ) \
                   |  put( ((x+7)%W + (y+p)%H * W) / 8, Mem[(I+p)&0xFFF] << (8 - x%8) ); \
            VF = (kk != 0)                                      ) /* xor-draw L bytes at (Vx,Vy). VF=collision. */ \
        o("ld f,vX",     "Fx29", u==0xF && kk==0x29, I = &Font[(Vx&15)*5] - Mem     ) /* set I = location of sprite for digit Vx */ \
        o("ld vX,dt",    "Fx07", u==0xF && kk==0x07, Vx = DelayTimer                ) /* set Vx = delay timer    */ \
        o("ld dt,vX",    "Fx15", u==0xF && kk==0x15, DelayTimer = Vx                ) /* set delay timer = Vx    */ \
        o("ld st,vX",    "Fx18", u==0xF && kk==0x18, SoundTimer = Vx                ) /* set sound timer = Vx    */ \
        o("ld i,N",      "Annn", u==0xA            , I = nnn                        ) /* set I = nnn             */ \
        o("add i,vX",    "Fx1E", u==0xF && kk==0x1E, p = (I&0xFFF)+Vx; VF=p>>12; I=p) /* set I = I + Vx, update VF = overflow    */ \
        o("se vX,K",     "3xkk", u==0x3            , if(kk == Vx) PC += 2           ) /* PC+=2 if Vx == kk       */ \
        o("se vX,vY",    "5xy0", u==0x5 &&  p==0x0 , if(Vy == Vx) PC += 2           ) /* PC+=2 if Vx == Vy       */ \
        o("sne vX,K",    "4xkk", u==0x4            , if(kk != Vx) PC += 2           ) /* PC+=2 if Vx != kk       */ \
        o("sne vX,vY",   "9xy0", u==0x9 &&  p==0x0 , if(Vy != Vx) PC += 2           ) /* PC+=2 if Vx != Vy       */ \
        o("skp vX",      "Ex9E", u==0xE && kk==0x9E, if( Keys[Vx&15]) PC += 2       ) /* PC+=2 if key[Vx] down   */ \
        o("sknp vX",     "ExA1", u==0xE && kk==0xA1, if(!Keys[Vx&15]) PC += 2       ) /* PC+=2 if key[Vx] up     */ \
        o("ld b,vX",     "Fx33", u==0xF && kk==0x33, Mem[(I+0)&0xFFF] = (Vx/100)%10; \
                                                     Mem[(I+1)&0xFFF] = (Vx/10)%10;  \
                                                     Mem[(I+2)&0xFFF] = (Vx/1)%10   ) /* store Vx in BCD at mem[I] */ \
        o("ld [i],vX",   "Fx55", u==0xF && kk==0x55, for(p=0;p<=x;++p) Mem[I++ & 0xFFF]=V[p]) /* store V0..Vx at mem[I]    */ \
        o("ld vX,[i]",   "Fx65", u==0xF && kk==0x65, for(p=0;p<=x;++p) V[p]=Mem[I++ & 0xFFF]) /* load V0..Vx from mem[I]   */

    void ExecIns()
    {
        // Read a two-byte opcode from memory and advance the Program Counter.
        unsigned opcode = Mem[PC & 0xFFF]*0x100 + Mem[(PC+1) & 0xFFF]; PC += 2;
        // Extract bit-fields from the opcode.
        unsigned u   = (opcode>>12) & 0xF;
        unsigned p   = (opcode>>0) & 0xF;
        unsigned y   = (opcode>>4) & 0xF;
        unsigned x   = (opcode>>8) & 0xF;
        unsigned kk  = (opcode>>0) & 0xFF;
        unsigned nnn = (opcode>>0) & 0xFFF;
        // Create aliases for registers accessed by various instructions.
        auto& Vx = V[x], &Vy = V[y], &VF = V[0xF];
        // Execute the instruction.
        #define o(mnemonic,bits,test,ops) if(test) { ops; } else
        LIST_INSTRUCTIONS(o) {}
        #undef o
    }

    // Render the screen in a RGB buffer.
    void RenderTo(Uint32* pixels)
    {
        for(unsigned pos=0; pos < W*H; ++pos)
            pixels[pos] = 0xFFFFFF * ((DispMem[pos/8] >> (7 - pos%8)) & 1);
    }

    // Load a program.
    void Load(const char* filename, unsigned pos = 0x200)
    {
        for(std::ifstream f(filename, std::ios::binary); f.good(); )
            Mem[pos++ & 0xFFF] = f.get();
    }
};

static std::deque<std::pair<unsigned/*samples*/,bool/*volume*/>> AudioQueue;

int main(int argc, char** argv)
{
    Chip8 cpu;
    cpu.Load(argv[1]);

    // Create a screen.
    SDL_Window* window = SDL_CreateWindow(argv[1], SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W*4,H*6, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W,H);

    // Create a mapping of SDL keyboard symbols to Chip-8 keypad codes.
    std::unordered_map<int,int> keymap{
        {SDLK_1, 0x1}, {SDLK_2, 0x2}, {SDLK_3, 0x3}, {SDLK_4, 0xC},
        {SDLK_q, 0x4}, {SDLK_w, 0x5}, {SDLK_e, 0x6}, {SDLK_r, 0xD},
        {SDLK_a, 0x7}, {SDLK_s, 0x8}, {SDLK_d, 0x9}, {SDLK_f, 0xE},
        {SDLK_z, 0xA}, {SDLK_x, 0x0}, {SDLK_c, 0xB}, {SDLK_v, 0xF},
        {SDLK_5, 0x5}, {SDLK_6, 0x6}, {SDLK_7, 0x7},
        {SDLK_8, 0x8}, {SDLK_9, 0x9}, {SDLK_0, 0x0}, {SDLK_ESCAPE,-1}
    };

    // Initialize SDL audio.
    SDL_AudioSpec spec, obtained;
    spec.freq     = 44100;
    spec.format   = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples  = spec.freq / 20; // 0.05 seconds of latency
    spec.callback = [](void*, Uint8* stream, int len)
    {
        // Generate square wave
        short* target = (short*)stream;
        while(len > 0 && !AudioQueue.empty())
        {
            auto& data = AudioQueue.front();
            for(; len && data.first; target += 2, len -= 4, --data.first)
                target[0] = target[1] = data.second*300*((len&128)-64);
            if(!data.first) AudioQueue.pop_front();
        }
    };
    SDL_OpenAudio(&spec, &obtained);
    SDL_PauseAudio(0);

    unsigned insns_per_frame       = 10; // 50 for most chip8 programs
    unsigned max_consecutive_insns = 0;
    int frames_done = 0;
    bool interrupted = false;

    auto start = std::chrono::system_clock::now();
    while(!interrupted)
    {
        // Run CPU a) for max_consecutive_insns
        //         b) until the program is waiting for a key,
        //         whichever comes first.
        for(unsigned a=0; a<max_consecutive_insns && !(cpu.WaitingKey & 0x80); ++a)
            cpu.ExecIns();
        // Process events.
        for(SDL_Event ev; SDL_PollEvent(&ev); )
            switch(ev.type)
            {
                case SDL_QUIT: interrupted = true; break;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    auto i = keymap.find(ev.key.keysym.sym);
                    if(i == keymap.end()) break;
                    if(i->second == -1) { interrupted = true; break; }
                    cpu.Keys[i->second] = ev.type==SDL_KEYDOWN;
                    if(ev.type==SDL_KEYDOWN && (cpu.WaitingKey & 0x80))
                    {
                        cpu.WaitingKey        &= 0x7F;
                        cpu.V[cpu.WaitingKey] = i->second;
                    }
            }
        // Check how many frames we are _supposed_ to have rendered so far
        auto cur = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = cur-start;
        int frames = int(elapsed_seconds.count() * 60) - frames_done;
        if(frames > 0)
        {
            frames_done += frames;
            // Update the timer registers
            int st = std::min(frames, cpu.SoundTimer+0); cpu.SoundTimer -= st;
            int dt = std::min(frames, cpu.DelayTimer+0); cpu.DelayTimer -= dt;
            // Render audio
            SDL_LockAudio();
             AudioQueue.emplace_back( obtained.freq*(         st)/60,  true );
             AudioQueue.emplace_back( obtained.freq*(frames - st)/60, false );
            SDL_UnlockAudio();
            // Render graphics
            Uint32 pixels[W*H]; cpu.RenderTo(pixels);
            SDL_UpdateTexture(texture, nullptr, pixels, 4*W);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }
        // Adjust the instruction count to compensate for our rendering speed
        max_consecutive_insns = std::max(frames, 1) * insns_per_frame;
        // If the CPU is still waiting for a key, or if we didn't
        // have a frame yet, consume a bit of time
        if((cpu.WaitingKey & 0x80) || !frames) SDL_Delay(1000/60);
    }
    SDL_Quit();
}



