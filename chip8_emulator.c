#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DEBUG

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
} sdl_t;

typedef struct {
    uint32_t window_width;
    uint32_t window_height;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t per_seconds;
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED
} emulator_state_t;

typedef struct {
    uint16_t opcode;
    uint16_t NNN;
    uint8_t NN;
    uint8_t N;
    uint8_t X;
    uint8_t Y;
} instructions_t;

typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64 * 32];
    uint16_t stack[16];
    uint16_t *stack_ptr;
    uint8_t v[16];
    uint16_t I;
    uint16_t PC;
    uint8_t delay_timer;
    uint8_t sound_timer;
    bool keypad[16];
    const char *rom_name;
    instructions_t inst;
} chip_8;

void print_debug_info(chip_8 *chip8) {
    printf("\n--- CHIP-8 DEBUG ---\n");
    printf(
        "Address: 0x%03X | Opcode: 0x%04X "
        "(Op: 0x%X, X: 0x%X, Y: 0x%X, N: 0x%X, NN: 0x%02X, NNN: 0x%03X)\n",
        chip8->PC - 2,
        chip8->inst.opcode,
        (chip8->inst.opcode >> 12) & 0x0F,
        chip8->inst.X,
        chip8->inst.Y,
        chip8->inst.N,
        chip8->inst.NN,
        chip8->inst.NNN
    );
    printf("--------------------\n");
}

bool init_chip8(chip_8 *chip8, const char *rom_name) {
    const uint16_t entry_point = 0x200;
    
    /* Font data stored starting at 0x000 in RAM */
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x80, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x90, 0xF0, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };
    memcpy(&chip8->ram[0], font, sizeof(font));

    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("ROM file %s is invalid\n", rom_name);
        return false;
    }

    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof(chip8->ram) - entry_point;
    rewind(rom);

    if (rom_size > max_size) {
        SDL_Log("ROM file %s is too big, Maximum size of %zu allowed, Current size is %zu\n", rom_name, max_size, rom_size);
        fclose(rom);
        return false;
    }

    if (fread(&chip8->ram[entry_point], 1, rom_size, rom) != rom_size) {
        SDL_Log("Failed to read ROM file %s\n", rom_name);
        fclose(rom);
        return false;
    }
    fclose(rom);

    chip8->state = RUNNING;
    chip8->PC = entry_point;
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    return true;
}

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    static double phase = 0.0;
    const int16_t amplitude = 3000;
    const double frequency = 440.0;
    const double sample_rate = 44100.0;
    int16_t *buffer = (int16_t *)stream;
    int samples = len / sizeof(int16_t);

    for (int i = 0; i < samples; i++) {
        buffer[i] = (phase < 0.5) ? amplitude : -amplitude;
        phase += frequency / sample_rate;
        if (phase >= 1.0) phase -= 1.0;
    }
}

bool init_sdl(sdl_t *sdl, const config_t *config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("Unable to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow(
        "CHIP-8 Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config->window_width,
        config->window_height,
        SDL_WINDOW_RESIZABLE
    );

    if (!sdl->window) {
        SDL_Log("Could not create window: %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) {
        SDL_Log("Failed to create renderer: %s\n", SDL_GetError());
        return false;
    }

    sdl->want = (SDL_AudioSpec){
        .freq = 44100,
        .format = AUDIO_S16LSB,
        .channels = 1,
        .samples = 4096,
        .callback = audio_callback,
    };

    sdl->dev = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0);
    if (sdl->dev == 0) {
        SDL_Log("Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    /* Allowed format mismatch handling (relaxes identical check) */
    if (sdl->want.format != sdl->have.format || sdl->want.channels != sdl->have.channels) {
        SDL_Log("Audio device format mismatch (requested %d channels/0x%x format, got %d/0x%x)\n",
                sdl->want.channels, sdl->want.format, sdl->have.channels, sdl->have.format);
    }
    return true;
}

bool set_config_from_args(config_t *config, int argc, char **argv) {
    (void)argc;
    (void)argv;
    *config = (config_t){
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0xFFFFFFFF,
        .bg_color = 0x000000FF,
        .per_seconds = 700, // Standard CHIP-8 CPU target speed (~700 Hz)
    };
    return true;
}

void cleanup(sdl_t *sdl) {
    if (sdl->dev) SDL_CloseAudioDevice(sdl->dev);
    if (sdl->renderer) SDL_DestroyRenderer(sdl->renderer);
    if (sdl->window) SDL_DestroyWindow(sdl->window);
    SDL_Quit();
}

void clear_screen(const sdl_t *sdl, const config_t *config) {
    const uint8_t r = (config->bg_color >> 24) & 0xFF;
    const uint8_t g = (config->bg_color >> 16) & 0xFF;
    const uint8_t b = (config->bg_color >> 8) & 0xFF;
    const uint8_t a = (config->bg_color >> 0) & 0xFF;
    SDL_SetRenderDrawColor(sdl->renderer, r, g, b, a);
    SDL_RenderClear(sdl->renderer);
}

void handle_input(chip_8 *chip8) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        chip8->state = QUIT;
                        break;
                    case SDLK_SPACE:
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("Paused");
                        } else {
                            chip8->state = RUNNING;
                            puts("Running");
                        }
                        break;
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
                }
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
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
                }
                break;
        }
    }
}

void emulate_instructions(chip_8 *chip8) {
    chip8->inst.opcode = ((uint16_t)chip8->ram[chip8->PC] << 8) | chip8->ram[chip8->PC + 1];
    chip8->PC += 2;
    
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0xFF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
    print_debug_info(chip8);
#endif

    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                memset(chip8->display, false, sizeof(chip8->display));
            } else if (chip8->inst.NN == 0xEE) {
                /* Stack Underflow Protection */
                if (chip8->stack_ptr > &chip8->stack[0]) {
                    chip8->stack_ptr--;
                    chip8->PC = *chip8->stack_ptr;
                }
            }
            break;
        case 0x01:
            chip8->PC = chip8->inst.NNN;
            break;
        case 0x02:
            /* Stack Overflow Protection */
            if (chip8->stack_ptr < &chip8->stack[16]) {
                *chip8->stack_ptr = chip8->PC;
                chip8->stack_ptr++;
                chip8->PC = chip8->inst.NNN;
            }
            break;
        case 0x03:
            if (chip8->v[chip8->inst.X] == chip8->inst.NN) chip8->PC += 2;
            break;
        case 0x04:
            if (chip8->v[chip8->inst.X] != chip8->inst.NN) chip8->PC += 2;
            break;
        case 0x05:
            if (chip8->inst.N == 0x0) {
                if (chip8->v[chip8->inst.X] == chip8->v[chip8->inst.Y]) chip8->PC += 2;
            }
            break;
        case 0x06:
            chip8->v[chip8->inst.X] = chip8->inst.NN;
            break;
        case 0x07:
            chip8->v[chip8->inst.X] += chip8->inst.NN;
            break;
        case 0x08:
            switch (chip8->inst.N) {
                case 0x00: chip8->v[chip8->inst.X] = chip8->v[chip8->inst.Y]; break;
                case 0x01: chip8->v[chip8->inst.X] |= chip8->v[chip8->inst.Y]; break;
                case 0x02: chip8->v[chip8->inst.X] &= chip8->v[chip8->inst.Y]; break;
                case 0x03: chip8->v[chip8->inst.X] ^= chip8->v[chip8->inst.Y]; break;
                case 0x04: {
                    uint16_t result = chip8->v[chip8->inst.X] + chip8->v[chip8->inst.Y];
                    chip8->v[0xF] = (result > 255) ? 1 : 0;
                    chip8->v[chip8->inst.X] = (uint8_t)result;
                    break;
                }
                case 0x05: {
                    uint8_t flag = (chip8->v[chip8->inst.X] >= chip8->v[chip8->inst.Y]) ? 1 : 0;
                    chip8->v[chip8->inst.X] -= chip8->v[chip8->inst.Y];
                    chip8->v[0xF] = flag;
                    break;
                }
                case 0x06: {
                    /* Fix 1: Legacy CHIP-8 shift implementation */
                    uint8_t flag = chip8->v[chip8->inst.X] & 0x01;
                    chip8->v[chip8->inst.X] >>= 1;
                    chip8->v[0xF] = flag;
                    break;
                }
                case 0x07: {
                    uint8_t flag = (chip8->v[chip8->inst.Y] >= chip8->v[chip8->inst.X]) ? 1 : 0;
                    chip8->v[chip8->inst.X] = chip8->v[chip8->inst.Y] - chip8->v[chip8->inst.X];
                    chip8->v[0xF] = flag;
                    break;
                }
                case 0x0E: {
                    /* Fix 1: Carry assignment preserved correctly after operation */
                    uint8_t flag = (chip8->v[chip8->inst.X] & 0x80) >> 7;
                    chip8->v[chip8->inst.X] <<= 1;
                    chip8->v[0xF] = flag;
                    break;
                }
            }
            break;
        case 0x09:
            if (chip8->inst.N == 0x0) {
                if (chip8->v[chip8->inst.X] != chip8->v[chip8->inst.Y]) chip8->PC += 2;
            }
            break;
        case 0x0A:
            chip8->I = chip8->inst.NNN;
            break;
        case 0x0B:
            chip8->PC = chip8->v[0x0] + chip8->inst.NNN;
            break;
        case 0x0C:
            chip8->v[chip8->inst.X] = (uint8_t)(rand() % 256) & chip8->inst.NN;
            break;
        case 0x0D: {
            uint8_t x_pos = chip8->v[chip8->inst.X] % 64;
            uint8_t y_pos = chip8->v[chip8->inst.Y] % 32;
            chip8->v[0xF] = 0;

            for (int row = 0; row < chip8->inst.N; row++) {
                if (y_pos + row >= 32) break; // Clip vertically
                uint8_t sprite_byte = chip8->ram[chip8->I + row];

                for (int col = 0; col < 8; col++) {
                    if (x_pos + col >= 64) break; // Clip horizontally
                    uint8_t sprite_pixel = sprite_byte & (0x80 >> col);

                    if (sprite_pixel) {
                        int index = (x_pos + col) + ((y_pos + row) * 64);
                        if (chip8->display[index]) chip8->v[0xF] = 1;
                        chip8->display[index] ^= true;
                    }
                }
            }
            break;
        }
        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                if (chip8->keypad[chip8->v[chip8->inst.X] & 0x0F]) chip8->PC += 2;
            } else if (chip8->inst.NN == 0xA1) {
                if (!chip8->keypad[chip8->v[chip8->inst.X] & 0x0F]) chip8->PC += 2;
            }
            break;
        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A: {
                    bool key_pressed = false;
                    for (uint8_t i = 0; i < 16; i++) {
                        if (chip8->keypad[i]) {
                            chip8->v[chip8->inst.X] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    if (!key_pressed) chip8->PC -= 2; // Block until key state updates
                    break;
                }
                case 0x1E:
                    chip8->I += chip8->v[chip8->inst.X];
                    break;
                case 0x07:
                    chip8->v[chip8->inst.X] = chip8->delay_timer;
                    break;
                case 0x15:
                    chip8->delay_timer = chip8->v[chip8->inst.X];
                    break;
                case 0x18:
                    chip8->sound_timer = chip8->v[chip8->inst.X];
                    break;
                case 0x29:
                    /* Fix 2: Font character offsetting based on font starting at 0x00 */
                    chip8->I = (chip8->v[chip8->inst.X] & 0x0F) * 5;
                    break;
                case 0x33: {
                    uint8_t bcd = chip8->v[chip8->inst.X];
                    chip8->ram[chip8->I + 2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I + 1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;
                }
                case 0x55:
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) chip8->ram[chip8->I + i] = chip8->v[i];
                    break;
                case 0x65:
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) chip8->v[i] = chip8->ram[chip8->I + i];
                    break;
                default:
                    printf("Instruction: 0x%04X - Not implemented yet\n", chip8->inst.opcode);
                    break;
            }
            break;
        default:
            printf("Instruction: 0x%04X - Not implemented yet\n", chip8->inst.opcode);
            break;
    }
}

void render_display(const sdl_t *sdl, const config_t *config, const chip_8 *chip8) {
    clear_screen(sdl, config);

    const uint8_t r = (config->fg_color >> 24) & 0xFF;
    const uint8_t g = (config->fg_color >> 16) & 0xFF;
    const uint8_t b = (config->fg_color >> 8) & 0xFF;
    const uint8_t a = (config->fg_color >> 0) & 0xFF;
    SDL_SetRenderDrawColor(sdl->renderer, r, g, b, a);

    int window_width, window_height;
    SDL_GetWindowSize(sdl->window, &window_width, &window_height);

    int scale_x = window_width / 64;
    int scale_y = window_height / 32;
    if (scale_x < 1) scale_x = 1;
    if (scale_y < 1) scale_y = 1;

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 64; x++) {
            if (chip8->display[x + (y * 64)]) {
                SDL_Rect rect = {x * scale_x, y * scale_y, scale_x, scale_y};
                SDL_RenderFillRect(sdl->renderer, &rect);
            }
        }
    }
    SDL_RenderPresent(sdl->renderer);
}

int main(int argc, char **argv) {
    sdl_t sdl = {0};
    config_t config = {0};
    chip_8 chip8 = {0};

    if (argc < 2) {
        SDL_Log("Usage: %s <rom>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);
    
    config.window_width *= 10;
    config.window_height *= 10;

    if (!init_sdl(&sdl, &config)) {
        cleanup(&sdl);
        exit(EXIT_FAILURE);
    }

    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name)) {
        cleanup(&sdl);
        return EXIT_FAILURE;
    }
    /* Fix 3: Main Timing Accumulator Loop */
    uint64_t last_time = SDL_GetPerformanceCounter();
    double timer_accumulator = 0.0;
    double cycle_accumulator = 0.0;

    const double timer_target = 1000.0 / 60.0; // 60 Hz timer target in ms
    const double cycle_target = 1000.0 / (double)config.per_seconds; // Instruction execution target in ms

    while (chip8.state != QUIT) {
        handle_input(&chip8);

        if (chip8.state == QUIT) break;

        if (chip8.state == PAUSED) {
            render_display(&sdl, &config, &chip8);
            SDL_Delay(16);
            continue;
        }

        uint64_t current_time = SDL_GetPerformanceCounter();
        double delta_time = (double)(current_time - last_time) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        last_time = current_time;

        timer_accumulator += delta_time;
        cycle_accumulator += delta_time;
        /* Execute instructions based on clock speed */
        while (cycle_accumulator >= cycle_target) {
            emulate_instructions(&chip8);
            cycle_accumulator -= cycle_target;
        }
        /* Decrement timers strictly at 60 Hz */
        while (timer_accumulator >= timer_target) {
            if (chip8.delay_timer > 0) chip8.delay_timer--;
            if (chip8.sound_timer > 0) chip8.sound_timer--;
            timer_accumulator -= timer_target;
        }
        /* Audio handling */
        if (chip8.sound_timer > 0) {SDL_PauseAudioDevice(sdl.dev, 0);
        } else {SDL_PauseAudioDevice(sdl.dev, 1);}
        render_display(&sdl, &config, &chip8);
        SDL_Delay(1);
    }
    puts("Exiting cleanly...");
    cleanup(&sdl);
    return EXIT_SUCCESS;
}