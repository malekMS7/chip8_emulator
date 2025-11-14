#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdbool.h>


//SDL Container object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;
//Emulator configuration object
typedef struct {
    uint32_t window_width; //SDL window width
    uint32_t window_height;//SDL window height
    uint32_t fg_color;//Foreground color RGBA8888
    uint32_t bg_color;//Background color RGBA8888
    uint32_t scale_factor;// Amount to scale a CHIP8 pixel by e.g. 20x will be a 20x larger window


} config_t;
//Emulator states
typedef enum{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;


// CHIP8 Machine object
typedef struct{
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];   // Emulate Original CHIP8  
    uint16_t stack[12];  // Subroutine stack
    uint8_t V[16];       // Data registers V0-VF
    uint8_t I;           // Index register
    uint16_t PC;         // Program Counter
    uint8_t delay_timer; // Decrements at 60hz when >0
    uint8_t sound_timer; // Decrements at 60hz and plays tone when >0 
    bool keypad[16];     // Hexadecimal keypad 0x0-0xF
    char *rom_name;      // Currently running ROM
}chip8_t;

// Forward declarations
void final_cleanup(const sdl_t sdl);




// Initialize SDL
bool init_sdl(sdl_t *sdl , const config_t config){
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) !=0) {
        SDL_Log("Could not initialize SDL: %s", SDL_GetError());
        return false;

    }

    sdl->window = SDL_CreateWindow("CHIP8 Emulator",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,config.window_width*config.scale_factor,config.window_height*config.scale_factor,0);
    if (!sdl->window){
        SDL_Log("Could not create window %s", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer){
        SDL_Log("Could not create SDL renderer %s\n", SDL_GetError());
        return false;
    }

    return true;
}

bool set_config_from_args(config_t *config, const int argc,  char **argv){
    *config =(config_t){
        .window_width =64, //CHIP8 original x resolution
        .window_height =32, //CHIP8 original y resolution
        .fg_color = 0xFFFFFFFF, //White
        .bg_color = 0xFFFF00FF, //Yellow
        .scale_factor = 20,     //Default resolution will be 1280x640
    };

    for(int i=1; i<argc; i++){
        (void)argv[i];

    }
    return true; //success

}
//Initialize CHIP8 machine
bool init_chip8(chip8_t *chip8, const char rom_name[]){
    const uint32_t entry_point = 0x200; // CHIP8 ROMs will be loaded to 0x200
    const uint8_t font [] ={
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0 11110000 
        0x20, 0x60, 0x20, 0x20, 0x70, // 1 10010000
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2 10010000
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3 10010000
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4 11110000
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
    const size_t max_size = chip8->ram - entry point;
    rewind(rom);
    if (rom_size > max_size ){
        SDL_Log("ROM file %s is too big! ROM size: %zu , Max size Allowed: %zu\n",rom_name,rom_size,max_size);
        return false;

    }
    if (fread(&chip8->ram[entry_point],rom_size, 1,rom ) != 1){
        
    }



    fclose(rom);

    // Set chip8 machine defaults
    chip8->state = RUNNING; //Default machine state to on/running
    chip8->PC = entry_point //Start program counter at ROM entry point
    return true; //success
}




//Final cleanup of SDL resources
void final_cleanup(const sdl_t sdl){
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
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

void update_screen(const sdl_t sdl){
    SDL_RenderPresent(sdl.renderer);

}
//Handle user input
void handle_input(chip8_t *chip8){
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                //Exit window;End program
                chip8->state = QUIT; //will exit main emulator loop
                return;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym){
                    case SDLK_ESCAPE:
                        //ESCAPE key; Exit the window & End program
                        chip8->state = QUIT;
                        return;
                    default:
                        break;

                }
                break;

            case SDL_KEYUP:
                break;
            default:
                break;



        }

    }

}






//main function
int main (int argc , char **argv){
    //Initialize emulator configuration/options
    config_t config = {0};
    if (!set_config_from_args(&config, argc,argv)) exit(EXIT_FAILURE); 

    //Initialize SDL
    sdl_t sdl = {0};
    if(!init_sdl(&sdl,config)) exit(EXIT_FAILURE);

    //Initialize CHIP8 machine
    chip8_t chip8 = {0};
    if (!init_chip8(&chip8)) exit(EXIT_FAILURE);

    //Initial screen clear
    clear_screen(sdl,config);
    
    //Main Emulator loop
    while (chip8.state != QUIT){
        handle_input(&chip8);
        clear_screen(sdl,config);
        SDL_Delay(16);
        update_screen(sdl);



    }
    //Final cleanup and exit
    final_cleanup(sdl);
    return EXIT_SUCCESS;
}

    


    

