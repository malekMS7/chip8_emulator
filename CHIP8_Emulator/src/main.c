#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>


//SDL Container object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want , have;
    SDL_AudioDeviceID dev;
} sdl_t;

//Emulator states
typedef enum{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

// CHIP-8 extensions
typedef enum{
    CHIP8,
    SUPERCHIP,
    XOCHIP8,
}extension_t;



//Emulator configuration object
typedef struct {
    uint32_t window_width;          //SDL window width
    uint32_t window_height;         //SDL window height
    uint32_t fg_color;             //Foreground color RGBA8888
    uint32_t bg_color;            //Background color RGBA8888
    uint32_t scale_factor;       // Amount to scale a CHIP8 pixel by e.g. 20x will be a 20x larger window
    bool pixel_outlines;        //Draw pixel outline
    uint32_t insts_per_second;  // CHIP8 CPU Clock rate or hz
    uint32_t square_wave_freq;  // Frequency of square wave sound e.g . 440hz for middle A
    uint32_t audio_sample_rate; //
    uint16_t volume;            // How loud is the device
    float color_lerp_rate;            // amount to lerp colors by,betweeen [0.1,1.0]
    extension_t current_extension;    // Current quirk/extension suport CHIP8 vs SUPERCHIP
} config_t;



//CHIP8 Instruction format
typedef struct{
    uint16_t opcode; 
    uint16_t NNN;   //12 bit address/constant
    uint8_t NN;     //8 bit constant
    uint8_t N;      //4 bit constant
    uint8_t X;      //4 bit register identifier
    uint8_t Y;      //4 bit register identifier
}instruction_t;


// CHIP8 Machine object
typedef struct{
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];   // Emulate Original CHIP8
    uint32_t pixel_color[64*32]; // CHIP8 pixel colors to draw
    uint16_t stack[12];  // Subroutine stack
    uint16_t *stack_ptr;
    uint8_t V[16];       // Data registers V0-VF
    uint16_t I;           // Index register
    uint16_t PC;         // Program Counter
    uint8_t delay_timer; // Decrements at 60hz when >0
    uint8_t sound_timer; // Decrements at 60hz and plays tone when >0 
    bool keypad[16];     // Hexadecimal keypad 0x0-0xF
    const char *rom_name;     // Currently running ROM
    instruction_t inst;      // Currently executing instruction
    bool draw;               // update the screen yes/no
}chip8_t;
// Color lerp helper function
uint32_t color_lerp(const uint32_t start_color,const uint32_t end_color,const float t){
    const uint8_t s_r = (start_color >> 24) & 0XFF;
    const uint8_t s_g = (start_color >> 16) & 0XFF;
    const uint8_t s_b = (start_color >> 8) & 0XFF;
    const uint8_t s_a = (start_color >> 0) & 0XFF;

    const uint8_t e_r = (end_color >> 24) & 0XFF;
    const uint8_t e_g = (end_color >> 16) & 0XFF;
    const uint8_t e_b = (end_color >> 8) & 0XFF;
    const uint8_t e_a = (end_color >> 0) & 0XFF;

    const uint8_t ret_r = ((1-t)*s_r) + (t*e_r);
    const uint8_t ret_g = ((1-t)*s_g) + (t*e_g);
    const uint8_t ret_b = ((1-t)*s_b) + (t*e_b);
    const uint8_t ret_a = ((1-t)*s_a) + (t*e_a);

    return (ret_r << 24) | (ret_g << 16) | (ret_b << 8) | ret_a;

}







// SDL audio callback

void audio_callback(void *userdata,uint8_t *stream,int len){
    config_t * config = (config_t *)userdata;

    int16_t *audio_data = (int16_t *)stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    // We are filling out 2 bytes at a time (int16_t) len is in bytes;
    // so we divise by 2
    for (int i = 0; i < len / 2; i++){
        audio_data [i] = ((running_sample_index++ / half_square_wave_period) % 2) ? config->volume : -config->volume;
    }
}










// Forward declarations
void final_cleanup(const sdl_t sdl);




// Initialize SDL
bool init_sdl(sdl_t *sdl ,  config_t *config){
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) !=0) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return false;

    }

    sdl->window = SDL_CreateWindow("CHIP8 Emulator",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,config->window_width*config->scale_factor,config->window_height*config->scale_factor,0);
    if (!sdl->window){
        SDL_Log("Could not create window %s", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer){
        SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
        return false;
    }
    //  Init Audio:
    sdl ->want = (SDL_AudioSpec){
        .freq = 44100,       // 44100hz "CD" Quality
        .format = AUDIO_S16LSB, //Signed 1- bit little endian
        .channels = 1,     // mono-channel
        .samples = 512,
        .callback = audio_callback,
        .userdata = config,     //userdata passed to audio callback
    };

    sdl->dev = SDL_OpenAudioDevice(NULL,0, &sdl->want,&sdl->have ,0);
    if (sdl->dev == 0){
        SDL_Log("Could not ge an audio device %s\n",SDL_GetError());
        return false;
    }
    if ((sdl->want.channels != sdl->have.channels) || (sdl->want.format != sdl->have.format)){
        SDL_Log("Could not get desired Audio Spec\n");
        return false;
    }

    return true; //Success
}

bool set_config_from_args(config_t *config, const int argc,  char **argv){
    *config =(config_t){
        .window_width =64, //CHIP8 original x resolution
        .window_height =32, //CHIP8 original y resolution
        .fg_color = 0xFFFFFFFF, //White
        .bg_color = 0x000000FF, //Black
        .scale_factor = 20,     //Default resolution will be 1280x640
        .pixel_outlines = true, //Draw pixel outline by default
        .insts_per_second = 700, //Number of instructions to emulate in 1 second (clock rate of CPU)
        .square_wave_freq = 440, //440 hz for middle A
        .audio_sample_rate = 44100, // CD Quality
        .volume = 3000,          // Max Volume
        .color_lerp_rate = 0.7,  // color_lerp_rate between 0.1 and 1.0
        .current_extension = CHIP8, // Set default
    };

    for(int i=1; i<argc; i++){
        (void)argv[i];
        if (strncmp(argv[i],"--scale-factor", strlen("--scale-factor")) == 0){
            i++;
            config->scale_factor =(uint32_t) strtol(argv[i],NULL,10);

        }

    }
    return true; //success

}
//Initialize CHIP8 machine
bool init_chip8(chip8_t *chip8,const config_t config ,const char rom_name[]){
    const uint32_t entry_point = 0x200; // CHIP8 ROMs will be loaded to 0x200
    const uint8_t font [] ={
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0 ->11110000 
        0x20, 0x60, 0x20, 0x20, 0x70, // 1   10010000
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2   10010000
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3   10010000
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4   11110000
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5 
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    // Initialize entire chipo8 machine
    memset(chip8,0,sizeof(chip8_t));

    //Load Font
    memcpy(&chip8->ram[0],font, sizeof(font));

    // Open ROM file
    FILE *rom = fopen(rom_name,"rb");
    if (!rom) {
        SDL_Log("Rom file %s is invalid or does not exist\n",rom_name);
        return false;
    }
    // Get check rom size
    fseek(rom,0,SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof(chip8->ram) - entry_point;
    rewind(rom);
    if (rom_size > max_size ){
        SDL_Log("ROM file %s is too big! ROM size: %zu , Max size Allowed: %zu\n",rom_name,rom_size,max_size);
        return false;

    }
    // Load ROM
    if (fread(&chip8->ram[entry_point],rom_size, 1,rom ) != 1){
        SDL_Log("Could not read ROM file %s into CHIP8 memory\n",rom_name);
        return false;
        
    }



    fclose(rom);

    // Set chip8 machine defaults
    chip8->state = RUNNING; //Default machine state to on/running
    chip8->PC = entry_point; //Start program counter at ROM entry point
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    memset(&chip8->pixel_color[0], config.bg_color ,sizeof chip8->pixel_color); // Init pixels to bg color

    return true; //success
}

//Final cleanup of SDL resources
void final_cleanup(const sdl_t sdl){
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_CloseAudioDevice(sdl.dev);
    SDL_Quit();//Shutdown SDL subsystems

}
// Clear screen / SDL Window to background color
void clear_screen(const sdl_t sdl ,const config_t config){
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer,r,g,b,a);
    SDL_RenderClear(sdl.renderer);

}

void update_screen(const sdl_t sdl,const config_t config, chip8_t *chip8) {
    SDL_Rect rect = { .x = 0, .y = 0, .w=config.scale_factor, .h = config.scale_factor};
    // Grab bg colors values to draw outlines

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

    // Loop through display pixels draw a rectangle per pixel to the SDL window
    for(uint32_t i = 0; i < sizeof(chip8->display); i++){
        // Translate 1D index i value to 2D X/Y coordinates
        // X = i % window width
        // Y = j / window width
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8->display[i]) {
            //pixel is on , draw foreground color
            if (chip8->pixel_color[i] != config.fg_color){
                // lerp towards fg color
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i],config.fg_color,config.color_lerp_rate);
            }
            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
            const uint8_t b = (chip8->pixel_color[i] >> 8) & 0xFF;
            const uint8_t a = (chip8->pixel_color[i] >> 0) & 0xFF;

            SDL_SetRenderDrawColor(sdl.renderer,r,g,b,a);
            SDL_RenderFillRect(sdl.renderer, &rect);
            // if user requested drawing pixel outlines , draw those here
            if(config.pixel_outlines){
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }

        } else{
             if (chip8->pixel_color[i] != config.bg_color){
                // lerp towards bg color
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i],config.bg_color,config.color_lerp_rate);
            }
            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
            const uint8_t b = (chip8->pixel_color[i] >> 8) & 0xFF;
            const uint8_t a = (chip8->pixel_color[i] >> 0) & 0xFF;
            //pixel is off , draw background color
            SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

//Handle user input
//CHIP8 keypad  AZERTY
//123C          1234
//456D          azer
//789E          qsdf
//A0BF          wxcv 
void handle_input(chip8_t *chip8,config_t *config){
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                //Exit window;End program
                chip8->state = QUIT; //will exit main emulator loop
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym){
                    case SDLK_ESCAPE:
                        //ESCAPE key; Exit the window & End program
                        chip8->state = QUIT;
                        break;
                    case SDLK_SPACE:
                        //space bar
                        if (chip8->state == RUNNING){
                            chip8->state = PAUSED; //PAUSE
                            puts("=== PAUSED ===");
                        } else{
                            chip8->state = RUNNING; //RESUME
                        }
                        break;
                    case SDLK_EQUALS:
                        // '=': Reset CHIP8 MACHINE
                        init_chip8(chip8,*config,chip8->rom_name);
                        break;
                    case SDLK_j:
                        // 'j' decrease color lerp rate
                        if (config->color_lerp_rate > 0.1){
                            config->color_lerp_rate -= 0.1;
                        }
                    case SDLK_k:
                        // 'k' increase color lerp rate
                        if (config->color_lerp_rate < 0.1){
                            config->color_lerp_rate += 0.1;
                        }
                     case SDLK_o:
                        // 'o' decrease volume
                        if (config->volume > 0){
                            config->volume -= 500;
                        }
                    case SDLK_p:
                        // 'p' increase volume
                        if (config->volume < INT16_MAX){
                            config->volume += 500;
                        }
                    


                    // Map qwerty keys to chip8 keypad
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break;

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break;

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break;

                    case SDLK_z: chip8->keypad[0xA] = true; break;
                    case SDLK_x: chip8->keypad[0x0] = true; break;
                    case SDLK_c: chip8->keypad[0xB] = true; break;
                    case SDLK_v: chip8->keypad[0xF] = true; break;

                    default: break;

                }
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                    // Map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = false; break;
                    case SDLK_2: chip8->keypad[0x2] = false; break;
                    case SDLK_3: chip8->keypad[0x3] = false; break;
                    case SDLK_4: chip8->keypad[0xC] = false; break;

                    case SDLK_q: chip8->keypad[0x4] = false; break;
                    case SDLK_w: chip8->keypad[0x5] = false; break;
                    case SDLK_e: chip8->keypad[0x6] = false; break;
                    case SDLK_r: chip8->keypad[0xD] = false; break;

                    case SDLK_a: chip8->keypad[0x7] = false; break;
                    case SDLK_s: chip8->keypad[0x8] = false; break;
                    case SDLK_d: chip8->keypad[0x9] = false; break;
                    case SDLK_f: chip8->keypad[0xE] = false; break;

                    case SDLK_z: chip8->keypad[0xA] = false; break;
                    case SDLK_x: chip8->keypad[0x0] = false; break;
                    case SDLK_c: chip8->keypad[0xB] = false; break;
                    case SDLK_v: chip8->keypad[0xF] = false; break;
                    default: break;

                }
                break;
            default:
                break;
        }

    }

}

#ifdef DEBUG
    void print_debug_info(chip8_t *chip8){
        printf("Address: 0x%04X, Opcode: 0x%04X Desc: ",chip8->PC-2,chip8->inst.opcode);
        switch ((chip8->inst.opcode >> 12) & 0x0F){
        case 0x0:
            if (chip8->inst.NN == 0xE0){
                // 0x00E0: Clear the screen
                printf("Clear screen\n");
            } else if (chip8->inst.NN == 0xEE){
                //0x00EE: Return from subroutine
                // Set PC to last address on subroutine stack ("pop" it off the stack)
                // so that the next opcode will be gotten from that address
                printf("Return from subroutine to address 0x%04X\n",*(chip8->stack_ptr -1));
            }else{
                printf("Unimplemented Opcode.\n");
            }
            break;
        case 0x01:
            // 0x1NNN : Jump to address NNN
            printf("Jump to address NNN (0x%04X)\n",chip8->inst.NNN);
            break;

        case 0x02:
            //0x2NNN : Call subroutine at NNN
            // Store current subroutine to return to on subroutine stack("push" it on the stack)
            // and set program counter to subroutine address so that the 
            // next opcode is gotten from there
            printf("Call subroutine at NNN (0x%04X)\n",chip8->inst.NNN);
            break;
        case 0x03:
            // 0x3XNN: check if VX == NN if so skip the next instruction
            printf("ChecK if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.NN);
            break;
        case 0x04:
            // 0x4XNN: check if VX != NN if so skip the next instruction
            printf("ChecK if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.NN);
            break;
        case 0x05:
            // 0x5XY0: check if VX == VY if so skip the next instruction
            printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y]);
            break;
        case 0x06:
            //0x6XNN: Set register VX to NN
            printf("Set register V%X = NN (0x%02X)\n",chip8->inst.X,chip8->inst.NN);
            break;
        case 0x07:
            // 0x7XNN: Set register VX +=NN
            printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.NN,chip8->V[chip8->inst.X] + chip8->inst.NN);
            break;
        case 0x08:
            switch(chip8->inst.N){
                case 0:
                    // 0x8XY0: Set register VX = VY
                    printf("Set register V%X = V%X (0x%02X)\n",chip8->inst.X,chip8->inst.Y,chip8->V[chip8->inst.Y]);
                    break;
                case 1:
                    // 0x8XY1:set register VX |= VY
                    printf("Set register V%X (0x%02X) |= V%X (0x%02X) ; Result : 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
                    break;
                case 2:
                    // 0x8XY2:set register VX &= VY
                    printf("Set register V%X (0x%02X) &= V%X (0x%02X) ; Result : 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
                    break;
                case 3:
                    // 0x8XY3:set register VX ^= VY
                    printf("Set register V%X (0x%02X) ^= V%X (0x%02X) ; Result : 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
                    break;
                case 4:
                    // 0x8XY4:set register VX += VY,set VF to 1 if carry
                    printf("Set register V%X (0x%02X) += V%X (0x%02X), VF = 1 if carry  ; Result : 0x%02X , VF = %X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
                    break;
                case 5:
                    // 0x8XY5: set register VX -= VY set VF to 1 if there is not a borrow (result is positive/0)
                    printf("Set register V%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow; Result : 0x%02X , VF = %X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],(chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
                    break;
                case 6:
                    // 0x8XY6: Set register VX >>= 1 , store shifted off bit in VF
                    printf("Set register V%X (0x%02X) >>= 1, VF = shifted off bit (%X); Result: 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->V[chip8->inst.X] & 1 , chip8->V[chip8->inst.X] >> 1);
                    break;
                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
                    printf("Set register V%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow ; Result : 0x%02X , VF = %X\n",chip8->inst.X,chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->inst.X,chip8->V[chip8->inst.X],chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],(chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                    break;
                case 0xE:
                    // 0x8XYE: set register VX <<= 1,store shifted off bit in VF
                    printf("Set register V%X (0x%02X) <<= 1, VF = shifted off bit (%X); Result: 0x%02X\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->V[chip8->inst.X] >> 7 , chip8->V[chip8->inst.X] << 1);
                    break;


                default:
                    // wrong/unimplemented opcode
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0 : Check if VX != VY; Skip next instruction if so
            printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y]);
            break;

        case 0x0A: 
            //0xANNN: Set index register I to NNN
            printf("Set I to NNN (0x%04X)\n",chip8->inst.NNN);
            break;
        case 0x0B:
            // 0xBNNN: Jump to V0 + NNN
            printf("Set PC to V0 (0x%02X) + NNN (0x%04X)\n",chip8->V[0],chip8->inst.NNN,chip8->V[0] + chip8->inst.NNN);
            break;
        case 0x0C:
            //0xCNNN: sets register VX = rand() % 256 & NN (bitwise AND)
            printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",chip8->inst.X ,chip8->inst.NN);
            break;

        case 0x0D:
            // 0xDXYN : Draw N-height sprite at coords X,Y; Read from memory location I;
            // Screen pixels are XOR'd with sprite bits,
            // VF (carry flag) is set if any screen pixels are set off ; this is useful 
            // collision detection or other reasons
            printf("Draw N (%u) height sprite at cords V%X (0x%02X) , V%X (0x%02X) from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",chip8->inst.N,chip8->inst.X,chip8->V[chip8->inst.X],chip8->inst.Y,chip8->V[chip8->inst.Y],chip8->I);
            break;
        case 0x0E:
            if (chip8->inst.NN == 0x9E){
                //0xEX9E: Skip next instruction if key in VX is pressed
                printf("skip next instruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->keypad[chip8->V[chip8->inst.X]]);
                
            } else if (chip8->inst.NN == 0xA1){
                //0xEX9E: skip next instruction if key is not pressed
                printf("skip next instruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->keypad[chip8->V[chip8->inst.X]]);
            }
            break;
        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A:
                    // 0xFX0A: VX = get_key(): Await until a keypress, and store in VX
                    printf("Await until akey is pressed; Store key in V%X\n",chip8->inst.X);
                    break;
                case 0x1E:
                    // 0xFx1E: I += VX Add VX to register I For non-Amiga CHIP8, does not affect VF
                    printf("I (0x%04X) += V%X (0x%02X); Result (I) : 0x%04X\n",chip8->I,chip8->inst.X,chip8->V[chip8->inst.X],chip8->I + chip8->V[chip8->inst.X]);
                    break;
                case 0x07:
                    // 0xFX07: VX = delay timer
                    printf("Set V%X = delay_timer value (0x%02X)\n",chip8->inst.X,chip8->delay_timer);
                    break;
                case 0x15:
                    // 0xFX15: delay_timer = VX
                    printf("Set delay_timer value = V%X (0x%02X)\n",chip8->inst.X,chip8->V[chip8->inst.X]);
                    break;
                case 0x18:
                    //0xFX18: sound_timer = VX
                    printf("Set sound_timer value = V%X (0x%02X)\n",chip8->inst.X,chip8->V[chip8->inst.X]);
                    break;
                case 0x29:
                    //0xFX29: Set register I to sprite location in memory in VX[0x0 -0xF]
                    printf("Set I to sprite location in memory for character in V%X (0x%02X) . Result(VX*5) = (0x%02X)\n",chip8->inst.X,chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] *5);
                    break;
                case 0x33:
                    //0xFX33: Store BCD representation of VX at memory offset from I;
                    // I = hundred's place, I+1 = ten's place, I+2 = one's place
                    printf("Store BCD representation of V%X (0x%02X) at memory from I (0x%04X)\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->I);
                    break;
                case 0x55:
                    //0xFX55: Register dump V0-VX inclusive to memory offset from I;
                    // SCHIP8 does not increment I , CHIP8 does increment I
                    printf("Register dump V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->I);
                    break;
                case 0x65:
                    //0xFX65: Register load V0-VX inclusive from memory offset from I;
                    // SCHIP8 does not increment I , CHIP8 does increment I
                    printf("Register load V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",chip8->inst.X,chip8->V[chip8->inst.X],chip8->I);
                    break;
                
                default:
                    break;
                
    
            }
            


        default:
            printf("Unimplemented Opcode.");
            break; // Unimplemented or invalid opcode
    }
}
#endif


//Emulate 1 CHIP8 instruction
void emulate_instructions(chip8_t *chip8, const config_t config) {
    bool carry;      // save carry falg VF value for some intsructions
    // Get next opcode from RAM
    chip8->inst.opcode = (chip8->ram[chip8->PC] <<8) | chip8->ram[chip8->PC+1];
    chip8->PC +=2; //Pre increment program counter for next opcode

    // Fill out current instruction format
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
    print_debug_info(chip8);
#endif




    //Emulate opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F){
        case 0x00:
            if (chip8->inst.NN == 0xE0){
                // 0x00E0: Clear the screen
                memset(&chip8->display[0],false, sizeof chip8->display);
                chip8->draw = true; //will update screen on next 60hz tick
            } else if (chip8->inst.NN == 0xEE) {
                //0x00EE: Return from subroutine
                // Set PC to last address on subroutine stack ("pop" it off the stack)
                // so that the next opcode will be gotten from that address
                chip8->PC = *--chip8->stack_ptr;
            } else {
                //Unimplemented invalid opcode maybe 0xNNN for calling machine code routine for RCA1802

            }
            break;
        case 0x01:
            // 0x01NNN : Jump to address NNN
            chip8->PC = chip8->inst.NNN ;// Set program counter so that next opcode is from NNN
            break;
            

        case 0x02:
            //0x2NNN : Call subroutine at NNN
            // Store current subroutine to return to on subroutine stack("push" it on the stack)
            // and set program counter to subroutine address so that the 
            // next opcode is gotten from there
            *chip8->stack_ptr++ = chip8->PC; 
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x03:
            // 0x3XNN: Check if VX == NN if so skip the next instruction
            if (chip8->V[chip8->inst.X] == chip8->inst.NN){
                chip8->PC += 2;   //skip next opcode/instruction
            }    
            break;
        case 0x04:
            // 0x4XNN: check if VX != NN if so, skip the next instruction
            if (chip8->V[chip8->inst.X] != chip8->inst.NN){
                chip8->PC += 2;   //skip next opcode/instruction
            }    
            break;
        case 0x05:
            // 0x5XY0: check if VX == VY if so skip the next instruction
            if (chip8->inst.N !=0) break; //wrong opcode
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break;
        case 0x06:
            // 0x6XNN : Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            // 0x7XNN: Set register VX +=NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch(chip8->inst.N){
                case 0:
                     // 0x8XY0: Set register VX = VY
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;
                case 1:
                    // 0x8XY1:set register VX |= VY
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8){
                        chip8->V[0xF] = 0;  // Reset VF to 0
                    }
                    break;
                case 2:
                    // 0x8XY2:set register VX &= VY
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8){
                        chip8->V[0xF] = 0;  // Reset VF to 0
                    }
                    break;
                case 3:
                    // 0x8XY3:set register VX ^= VY
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    if (config.current_extension == CHIP8){
                        chip8->V[0xF] = 0;  // Reset VF to 0
                    }
                    break;
                case 4: {
                    // 0x8XY4:set register VX += VY,set VF to 1 if carry
                    carry = ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255);
                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = carry;
                    break;
                }
                case 5:{
                    // 0x8XY5: set register VX -= VY set VF to 1 if there is not a borrow (result is positive/0)
                    carry = (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = carry;
                    break;
                }
                case 6:{
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    if (config.current_extension == CHIP8) {
                        carry = chip8->V[chip8->inst.Y] & 1;    // Use VY
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] >> 1; // Set VX = VY result
                    } else {
                        carry = chip8->V[chip8->inst.X] & 1;    // Use VX
                        chip8->V[chip8->inst.X] >>= 1;          // Use VX
                    }
                    chip8->V[0xF] = carry;
                    break;
                }
                case 7:{
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
                    carry = (chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X]);
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    chip8->V[0xF] = carry;
                    break;
                }
                case 0xE:{
                    // 0x8XYE: set register VX <<= 1,store shifted off bit in VF
                    if (config.current_extension == CHIP8) {
                        carry = (chip8->V[chip8->inst.Y] & 0x80) >> 7;    // Use VY
                        chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] << 1;   //Set VX = VY result
                    } else {
                        carry = (chip8->V[chip8->inst.X] & 0x80) >> 7;    // VX
                        chip8->V[chip8->inst.X] <<= 1;                   // Use VX
                    }
                    chip8->V[0xF] = carry;
                    break;
                }
                default:
                    // wrong/unimplemented opcode
                    break;
            }
            break;
        case 0x09:
            // 0x9XY0: Check if VX != VY;skip next instruction if so
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]){
                chip8->PC += 2;
            }
            break;
        case 0x0A:
            // 0xANNN Set index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;
        case 0x0B:
            // 0xBNNN: Jump to V0 + NNN
            chip8->PC =chip8->V[0] + chip8->inst.NNN;
            break;
        case 0x0C:
            //0xCNNN: sets register VX = rand() % 256 & NN (bitwise AND)
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;
        case 0x0D:{
            // 0xDXYN : Draw N-height sprite at coords X,Y; Read from memory location I;
            // Screen pixels are XOR'd with sprite bits,
            // VF (carry flag) is set if any screen pixels are set off ; this is useful 
            // collision detection or other reasons
            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord;
            chip8->V[0xF] = 0; //Initialize carry flag to 0
            for (uint8_t i = 0; i < chip8->inst.N; i++){
                //Get next byte/row of sprite data
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X; // Reset X for next row to draw
                for (int8_t j = 7; j >=0 ; j--){
                    //IF sprite pixel/bit is on and display pixel is on, set carry flag
                    bool *pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1 << j));
                    if (sprite_bit && *pixel){
                        chip8->V[0xF] = 1;
                    }

                    // XOR display pixel with sprite pixel/bit to set it on or off
                    *pixel ^= sprite_bit;
                    
                    // Stop drawing if hit right edge of screen
                    if (++X_coord >= config.window_width) break;

                }
                // Stop drawing entire sprite if hit bottom edge of screen
                if (++Y_coord >= config.window_height) break;

            }
            chip8->draw = true; // will update screen on 60hz tick
            break;
        }
        case 0x0E:
            if (chip8->inst.NN == 0x9E){
                // 0xEX9E: Skip next instruction if key in VX is pressed
                if (chip8->keypad[chip8->V[chip8->inst.X]]){
                    chip8->PC += 2;
                }

            }else if (chip8->inst.NN == 0xA1){
                // 0xEX9E: Skip next instruction if key in VX is not pressed
                if (!chip8->keypad[chip8->V[chip8->inst.X]]){
                    chip8->PC += 2;
                }

            }
            break;
        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A:{
                    // 0xFX0A: VX = get_key(); await until a keypress, and store in VX
                    static bool any_key_pressed = false;
                    static uint8_t key = 0xFF;
                    for (uint8_t i =0; key == 0xFF && i< sizeof chip8->keypad; i++){
                        if (chip8->keypad[i]){
                            key = i;        // Save pressed key to check until it is released
                            chip8->V[chip8->inst.X] = i;  // i = key (offset into keypad array)
                            any_key_pressed = true;
                            break;
                        }
                    }
                    // if no key has been pressed yet keep getting the current opcode & running this instruction
                    if (!any_key_pressed){
                        chip8->PC -= 2;
                    } else {
                        // A key has been pressed, also wait until it is released to set the key in VX
                        if (chip8->keypad[key]){  // Busy loop CHIP8 emulation until key is released
                            chip8->PC -= 2;
                        } else {
                            chip8->V[chip8->inst.X] = key;    //VX = key
                            key = 0XFF;    // Reset key to not found
                            any_key_pressed = false;  // Reset to nothing pressed yet
                        }
                    }
                    break;
                }
                case 0x1E:
                    // 0xFX1E I += VX; add VX to register I for non-Amiga CHIP8, does not affect VF
                    chip8->I += chip8->V[chip8->inst.X];
                    break;
                case 0x07:
                    // 0xFX07: VX = delay timer
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;
                case 0x15:
                    // 0xFX15: delay_timer = VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x18:
                    //0xFX18: sound_timer = VX
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;
                case 0x29:
                    //0xFX29: Set register I to sprite location in memory in VX[0x0 -0xF]
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;
                case 0x33:{
                    // 0xFX33: Store BCD representations of VX at memory offset from I;
                    //  I = hundred's place, I+1 = ten's place, I+2 = one's place
                    uint8_t bcd = chip8->V[chip8->inst.X]; // e.g .12[3]
                    chip8->ram[chip8->I+2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I+1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;
                }
                case 0x55:
                    //0xFX55: Register dump V0-VX inclusive to memory offset from I;
                    // SCHIP8 does not increment I , CHIP8 does increment I
                    // Note: could make this a config flag to use SCHIP8 or CHIP8 logic for I
                    for (uint8_t i = 0; i <= chip8->inst.X; i++)  {
                        if (config.current_extension == CHIP8) 
                            chip8->ram[chip8->I++] = chip8->V[i]; // Increment I each time
                        else
                            chip8->ram[chip8->I + i] = chip8->V[i]; 
                    }
                    break;
                case 0x65:
                    // 0xFX65: Register load V0-VX inclusive from memory offset from I;
                    //   SCHIP does not increment I, CHIP8 does increment I
                    for (uint8_t i = 0; i <= chip8->inst.X; i++)  {
                        if (config.current_extension == CHIP8) 
                            chip8->V[i] = chip8->ram[chip8->I++]; // Increment I each time
                        else
                            chip8->V[i] = chip8->ram[chip8->I + i]; 
                    }
                    break;

                default:
                    break;
            }
            break;

            








        default:
            break; // Unimplemented or invalid opcode
    }
}
//Update chip8 delay and sound timers every 60hz
void update_timers(const sdl_t sdl,chip8_t *chip8){
    if (chip8->delay_timer > 0) chip8->delay_timer--;
    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl.dev,0); // play Sound

    } else {
        SDL_PauseAudioDevice(sdl.dev,1); //Play sound

    }
}

//main function
int main (int argc , char **argv){
    //Default Usage message for args
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom_name>\n",argv[0]);
        exit(EXIT_FAILURE);

    }
    //Initialize emulator configuration/options
    config_t config = {0};
    if (!set_config_from_args(&config, argc,argv)) exit(EXIT_FAILURE); 

    //Initialize SDL
    sdl_t sdl = {0};
    if(!init_sdl(&sdl,&config)) exit(EXIT_FAILURE);

    //Initialize CHIP8 machine
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8,config ,rom_name)) exit(EXIT_FAILURE);

    //Initial screen clear
    clear_screen(sdl,config);
    // Seed random number generator
    srand(time(NULL));
    
    //Main Emulator loop
    while (chip8.state != QUIT){
        handle_input(&chip8,&config);

        if (chip8.state == PAUSED) continue;
        // Get_time() before running intsruction;
        const uint64_t start_frame_time = SDL_GetPerformanceCounter();
        // emulate CHIP8 Instructions for this emulator "frame" (60hz)
        for (uint32_t i = 0;i < config.insts_per_second / 60;i++){
            emulate_instructions(&chip8,config);
        }
        // Get_time elapsed after running instruction;
        const uint64_t end_frame_time = SDL_GetPerformanceCounter(); 
        // Delay for approximately 60hz/60fps (16.67ms) or actual time elapsed
        const double time_elapsed = (double)((end_frame_time - start_frame_time) * 1000) / SDL_GetPerformanceFrequency();
        // SDL_Delay(16-actual time elapsed);
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0);
        //Update window with changes every 60hz
        if (chip8.draw){
            update_screen(sdl,config,&chip8);
            chip8.draw = false;
        }






        update_screen(sdl,config,&chip8);
        //Update delay & sound timers
        update_timers(sdl,&chip8);
    }
    //Final cleanup and exit
    final_cleanup(sdl);
    return EXIT_SUCCESS;
}

    


    

