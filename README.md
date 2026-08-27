## CHIP-8 Emulator

A CHIP-8 emulator written in C using SDL2.

This project was built to learn low-level programming, CPU emulation, instruction decoding, memory management, input handling, graphics rendering, and audio.

## Features

* CHIP-8 CPU emulation
* 4 KB RAM
* 16 general-purpose registers (`V0`–`VF`)
* Index register (`I`)
* Program counter (`PC`)
* 16-level stack
* 64×32 monochrome display
* CHIP-8 hexadecimal keypad
* Delay timer
* Sound timer
* ROM loading
* SDL2 graphics
* SDL2 audio
* Pause/resume
* Debug opcode output
* Configurable emulation speed

## Controls

| CHIP-8    | Keyboard  |
| --------- | --------- |
| `1 2 3 C` | `1 2 3 4` |
| `4 5 6 D` | `Q W E R` |
| `7 8 9 E` | `A S D F` |
| `A 0 B F` | `Z X C V` |

Additional controls:

* `ESC` — Quit
* `SPACE` — Pause/Resume

## Tested ROMs

The emulator can be tested with several classic CHIP-8 ROMs and test programs:

* `test_opcode` — Opcode/instruction testing
* `IBM Logo` — Graphics and sprite rendering test
* `Soccer` — Gameplay, input and graphics
* `Breakout / Brix` — Collision, input and graphics
* `Tetris` — Larger gameplay test

## Build

Compile with GCC and SDL2:

```bash
gcc main.c -o chip8 $(sdl2-config --cflags --libs)
```

## Run

```bash
./chip8 <rom>
```

Example:

```bash
./chip8 roms/tetris.ch8
```

## Project Purpose

The goal of this project was not just to make a game emulator, but to understand how software can reproduce the behavior of a simple CPU and computer system.

This project is also preparation for the next stage of my systems programming work, where I will move from emulation into lower-level concepts such as Assembly, bootloaders, QEMU, virtual hardware, and kernel development.
