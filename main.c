#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <SDL2/SDL.h>

#define CHIP8_REG_MAX 16
#define CHIP8_STACK_MAX 16

#define T_DELAY 0
#define T_SND 1

SDL_Event event;
SDL_Renderer *renderer;
SDL_Window *window;
const Uint8 *keyboard_state;
SDL_Keycode keyboard_buf[2];
int monitor_enable;

static const uint8_t FONT_HEX[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
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

struct Timer {
    uint16_t counter;
    uint16_t freq;
};

struct Cpu {
    uint16_t VF;
    uint16_t I;
    uint16_t PC;
    uint8_t SP;
    uint8_t V[16];
    uint8_t RAM[0x1000];
    uint16_t STACK[16];
    uint16_t *DT;
    uint16_t *ST;
    struct Timer DEV[2];
};

struct Instruction {
    uint8_t opcode;
    uint16_t operand;
};

struct Instruction parse_data(uint16_t data) {
    struct Instruction instruction;
    instruction.opcode = data >> 12;
    instruction.operand = data & 0xfff;
    return instruction;
}

int timer_update(struct Timer *timer) {
    if (timer->counter > 0) {
        timer->counter--;
    }
    return timer->counter;
}

void clear_screen() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
    SDL_RenderPresent(renderer);
}

void sprite_draw(struct Cpu *cpu, uint8_t x, uint8_t y, uint8_t n) {
    uint8_t data;
    for (int i = 0; i < n; i++) {
        data = cpu->RAM[cpu->I + i];
        for (int j = 8; j > 0; j--) {
            if (data & 1) {
                SDL_RenderDrawPoint(renderer, cpu->V[x] + j, cpu->V[y] + i);
            }
            data >>= 1;
        }
    }
}

void get_keypress() {
    keyboard_buf[0] = event.key.keysym.sym;
    keyboard_buf[1] = '\0';
}

void do_disassemble(struct Cpu *cpu, struct Instruction *inst) {
    uint8_t x, y, n, kk;
    uint16_t addr;

    x = inst->operand >> 8;
    y = (inst->operand & 0x0F0) >> 4;
    n = inst->operand & 0x0F;
    kk = inst->operand & 0xFF;
    addr = inst->operand;

    printf("%04X   ", inst->opcode << 12 |  inst->operand);

    if (inst->opcode == 0 && inst->operand == 0xE0) {
        // clear screen
        printf("CLR\n");
    } else if (inst->opcode == 0 && inst->operand == 0xEE) {
        // return from subroutine
        printf("RET\n");
    } else if (inst->opcode == 0) {
        printf("SYS %04X\n", inst->operand);
        //printf("NOP\n", inst->operand);
    } else if (inst->opcode == 1) {
        // jump to operand address
        printf("JP %04X\n", inst->operand);
    } else if (inst->opcode == 2) {
        // call subroutine at operand address
        // inc stack, insert current PC, PC set to operand address
        printf("CALL %04X\n", inst->operand);
    } else if (inst->opcode == 3) {
        // skip if Vx = kk
        printf("SE V%u, %02X\n", x, kk);
    } else if (inst->opcode == 4) {
        // skip if Vx != kk
        printf("SNE V%u, %02X\n", x, kk);
    } else if (inst->opcode == 5) {
        // skip if Vx == Vy
        printf("SE V%u, V%u\n", x, y);
    } else if (inst->opcode == 6) {
        // set Vx = kk
        printf("LD V%u, %02x\n", x, kk);
    } else if (inst->opcode == 7) {
        // set Vx = Vx + kk
        printf("ADD V%u, %02x\n", x, kk);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 0) {
        // set Vx = Vy
        printf("LD V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 1) {
        // set Vx |= Vy
        printf("OR V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 2) {
        // set Vx &= Vy
        printf("AND V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 3) {
        // set Vx ^= Vy
        printf("XOR V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 4) {
        // set Vx += Vy
        printf("ADD V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 5) {
        // set Vx -= Vy
        printf("SUB V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 6) {
        // set Vx >> 1
        printf("SHR V%u\n", x);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 7) {
        // set Vx = Vy - Vx (VF = NOT)
        printf("SUBN V%u, V%u\n", x, y);
    } else if (inst->opcode == 8 && (inst->operand & 0xF) == 0xE) {
        // set Vx << 1
        printf("SHL V%u\n", x);
    } else if (inst->opcode == 9 && (inst->operand & 0xF) == 0) {
        // set Vx != Vy
        printf("SNE V%u, V%u\n", x, y);
    } else if (inst->opcode == 0xA) {
        // set I = operand address
        printf("LD I, %03X\n", addr);
    } else if (inst->opcode == 0xB) {
        // jump to operand address + V0
        printf("JP V0, %03X\n", addr);
    } else if (inst->opcode == 0xC) {
        // Vx = random byte and kk
        printf("RND V%u, %03X\n", x, kk);
    } else if (inst->opcode == 0xD) {
        // display sprite
        printf("DRW V%u, V%u, %01X\n", x, y, n);
    } else if (inst->opcode == 0xE && (inst->operand & 0xFF) == 0x9E) {
        // skip if key of value Vx is pressed
        printf("SKP V%u\n", x);
    } else if (inst->opcode == 0xE && (inst->operand & 0xFF) == 0xA1) {
        // skip if key with value Vx is not pressed
        printf("SKNP V%u\n", x);
    } else if (inst->opcode == 0xF && inst->operand != 0) {
        if ((inst->operand & 0xFF) == 0x7) {
            // set Vx = DT
            printf("LD V%u, DT\n", x);
        } else if ((inst->operand & 0xFF) == 0xA) {
            // wait for key, Vx = key
            printf("LD V%u, K\n", x);
        } else if ((inst->operand & 0xFF) == 0x15) {
            // set DT = Vx
            printf("LD DT, V%u\n", x);
        } else if ((inst->operand & 0xFF) == 0x18) {
            // set ST = Vx
            printf("LD ST, V%u\n", x);
        } else if ((inst->operand & 0xFF) == 0x1E) {
            // set I = I + Vx
            printf("ADD I, V%u\n", x);
        } else if ((inst->operand & 0xFF) == 0x29) {
            // set I = address of sprite for digit Vx
            printf("LD F, V%u\n", x);
        } else if ((inst->operand & 0xFF) == 0x33) {
            // store BCD of Vx in I, I+1, I+2
            printf("LD B, V%u\n", x);
        } else if ((inst->operand & 0xFF) == 0x55) {
            // store V0 ~ Vx starting at address I
            printf("LD [I], V%u\n", x);
        } else if ((inst->operand & 0xFF) == 0x65) {
            // read V0 ~ Vx from address I
            printf("LD V%u, [I]\n", x);
        }
    } else {
        printf("invalid opcode: %01X%03X\n", inst->opcode, inst->operand);
        exit(1);
    }
}
void do_operation(struct Cpu *cpu, struct Instruction *inst) {
    uint8_t x, y, n, kk;
    uint16_t addr;

    x = inst->operand >> 8;
    y = (inst->operand & 0x0F0) >> 4;
    n = inst->operand & 0x0F;
    kk = inst->operand & 0xFF;
    addr = inst->operand & 0xFFF;

    cpu->PC += 2;

    if (inst->opcode == 0 && inst->operand == 0xE0) {
        // clear screen
        clear_screen();
    } else if (inst->opcode == 0 && inst->operand == 0xEE) {
        // return from subroutine
        cpu->PC = cpu->STACK[--cpu->SP];
    } else if (inst->opcode == 0) {
        cpu->PC = addr;
    } else if (inst->opcode == 1) {
        // jump to operand address
        cpu->PC = addr;
    } else if (inst->opcode == 2) {
        // call subroutine at operand address
        // inc stack, insert current PC, PC set to operand address
        cpu->STACK[cpu->SP] = cpu->PC;
        cpu->SP++;
        cpu->PC = addr;
    } else if (inst->opcode == 3) {
        // skip if Vx = kk
        if (cpu->V[x] == kk) {
            cpu->PC += 2;
        }
    } else if (inst->opcode == 4) {
        // skip if Vx != kk
        if (cpu->V[x] != kk) {
            cpu->PC += 2;
        }
    } else if (inst->opcode == 5) {
        // skip if Vx == Vy
        if (cpu->V[x] == cpu->V[y]) {
            cpu->PC += 2;
        }
    } else if (inst->opcode == 6) {
        // set Vx = kk
        cpu->V[x] = kk;
    } else if (inst->opcode == 7) {
        // set Vx = Vx + kk
        cpu->V[x] += kk;
    } else if (inst->opcode == 8) {
        if ((inst->operand & 0xF) == 0) {
            // set Vx = Vy
            cpu->V[x] = cpu->V[y];
        } else if ((inst->operand & 0xF) == 1) {
            // set Vx |= Vy
            cpu->V[x] |= cpu->V[y];
        } else if ((inst->operand & 0xF) == 2) {
            // set Vx &= Vy
            cpu->V[x] &= cpu->V[y];
        } else if ((inst->operand & 0xF) == 3) {
            // set Vx ^= Vy
            cpu->V[x] ^= cpu->V[y];
        } else if ((inst->operand & 0xF) == 4) {
            // set Vx += Vy
            cpu->V[x] += cpu->V[y];
        } else if ((inst->operand & 0xF) == 5) {
            // set Vx -= Vy
            cpu->V[x] -= cpu->V[y];
        } else if ((inst->operand & 0xF) == 6) {
            // set Vx >> 1
            cpu->VF = cpu->V[y] & 1;
            cpu->V[x] >>= 1;
        } else if ((inst->operand & 0xF) == 7) {
            // set Vx = Vy - Vx (VF = NOT)
            cpu->VF = cpu->V[y] > cpu->V[x];
            cpu->V[x] = cpu->V[y] - cpu->V[x];
        } else if ((inst->operand & 0xF) == 0xE) {
            // set Vx << 1
            cpu->VF = cpu->V[x] & 0x80;
            cpu->V[x] <<= 1;
        }
    } else if (inst->opcode == 9) {
        // set Vx != Vy
        if (cpu->V[x] != cpu->V[y]) {
            cpu->PC += 2;
        }
    } else if (inst->opcode == 0xA) {
        // set I = operand address
        cpu->I = addr & 0x0fff;
    } else if (inst->opcode == 0xB) {
        // jump to operand address + V0
        cpu->PC = addr + cpu->V[0];
    } else if (inst->opcode == 0xC) {
        // Vx = random byte and kk
        cpu->V[x] = (uint8_t)rand() & kk;
    } else if (inst->opcode == 0xD) {
        // display sprite
        sprite_draw(cpu, x, y, n);
    } else if (inst->opcode == 0xE && (inst->operand & 0xFF) == 0x9E) {
        // skip if key of value Vx is pressed
        if (cpu->V[x] == keyboard_buf[0]) {
            cpu->PC += 2;
        }
    } else if (inst->opcode == 0xE && (inst->operand & 0xFF) == 0xA1) {
        // skip if key with value Vx is not pressed
        if (cpu->V[x] != keyboard_buf[0]) {
            cpu->PC += 2;
        }
    } else if (inst->opcode == 0xF && inst->operand != 0) {
        if ((inst->operand & 0xFF) == 0x7) {
            // set Vx = DT
            cpu->V[x] = *cpu->DT;
        } else if ((inst->operand & 0xFF) == 0xA) {
            // wait for key, Vx = key
            cpu->V[x] = keyboard_buf[0];
        } else if ((inst->operand & 0xFF) == 0x15) {
            // set DT = Vx
            *cpu->DT = cpu->V[x];
        } else if ((inst->operand & 0xFF) == 0x18) {
            // set ST = Vx
            *cpu->ST = cpu->V[x];
        } else if ((inst->operand & 0xFF) == 0x1E) {
            // set I = I + Vx
            cpu->I += cpu->V[x];
        } else if ((inst->operand & 0xFF) == 0x29) {
            // set I = address of sprite for digit Vx
            cpu->I = cpu->V[x] * 5;
        } else if ((inst->operand & 0xFF) == 0x33) {
            // store BCD of Vx in I, I+1, I+2
            char num[4];
            sprintf(num, "%03u", cpu->V[x]);
            printf("BCD: %s\n", num);
            cpu->RAM[cpu->I] = num[0] - '0';
            cpu->RAM[cpu->I + 1] = num[1] - '0';
            cpu->RAM[cpu->I + 2] = num[2] - '0';
        } else if ((inst->operand & 0xFF) == 0x55) {
            // store V0 ~ Vx starting at address I
            for (int i = 0; i <= x; i++) {
                cpu->RAM[cpu->I + i] = cpu->V[i];
            }
        } else if ((inst->operand & 0xFF) == 0x65) {
            // read V0 ~ Vx from address I
            for (int i = 0; i <= x; i++) {
                cpu->V[i] = cpu->RAM[cpu->I + i];
            }
        }
    } else {
        printf("invalid opcode: %01X%03X\n", inst->opcode, inst->operand);
        exit(1);
    }
}

size_t load_file_at(struct Cpu *cpu, const char *filename, uint16_t offset) {
    FILE *fp;
    char buf[10];
    size_t bytes;
    size_t bytes_read;
    size_t i;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        exit(1);
    }

    memset(buf, '\0', 10);

    i = 0;
    while (fread(&buf, sizeof(char), 1, fp) > 0) {
        cpu->RAM[offset + i] = buf[0];
        i++;
    }
    fclose(fp);
    return i;
}

void cpu_info(struct Cpu *cpu) {
    printf("PC=%04X, ", cpu->PC);
    printf("SP=%02X ", cpu->SP);
    printf("I=%04X, ", cpu->I);
    printf("DT=%04X, ", *cpu->DT);
    printf("ST=%04X, ", *cpu->ST);
    printf("VF=%u ", cpu->VF);
    printf("KB=%02X\n", keyboard_buf[0]);

    int c = 0;
    for (int i = 0; i < CHIP8_REG_MAX; i++) {
        if (c == 4) {
            printf("\n");
            c = 0;
        }
        printf("V[%02d]=%02x%c ", i, cpu->V[i], (i == CHIP8_REG_MAX - 1) ? ' ' : ',');
        c++;
    }
    printf("\n");

    printf("I[0:%u]: ", CHIP8_REG_MAX);
    for (int i = 0; i < CHIP8_REG_MAX; i++) {
        if (cpu->I + i > sizeof(cpu->RAM)) {
            break;
        }
        printf("%02X ", cpu->RAM[cpu->I + i]);
    }
    printf("\n");
}

void cpu_reset(struct Cpu *cpu) {
    memset(cpu->V, 0, sizeof(cpu->V));
    memset(cpu->DEV, 0, sizeof(cpu->DEV));
    cpu->DT = &cpu->DEV[T_DELAY].counter;
    cpu->ST = &cpu->DEV[T_SND].counter;
    cpu->PC = 0x200;
    cpu->I = 0x0000;
    cpu->SP = 0x00;
    cpu->VF = 0x00;
    for (int i = 0; i < sizeof(FONT_HEX) / (sizeof(uint8_t)); i++) {
        cpu->RAM[i] = FONT_HEX[i];
    }
    memset(keyboard_buf, '\0', 2);
}

static uint16_t breakpoints[255];
static uint16_t watchpoints[255];
int monitor(struct Cpu *cpu) {
    static int last_bp = 0;
    static int last_wp = 0;
    static int ignore = 0;
    char input[255];
    char *iptr = input;
    uint16_t addr, opcode, count;
    uint16_t pc;

    for (int i = 0; i <= last_bp; i++) {
        if (cpu->PC == breakpoints[i]) {
            printf("breakpoint #%u reached\n", i);
            ignore = 0;
        }
    }

    for (int i = 0; i <= last_wp; i++) {
        opcode = cpu->RAM[cpu->PC] << 8 | cpu->RAM[cpu->PC + 1];
        if (opcode == watchpoints[i]) {
            printf("watchpoint #%u reached\n", i);
            ignore = 0;
        }
    }

    if (ignore) {
        return 0;
    }

    memset(input, '\0', sizeof(input));
    pc = cpu->PC;

    printf("%04XH> ", cpu->PC);
    iptr = fgets(input, sizeof(input) - 1, stdin);
    if (iptr == NULL) {
        cpu->PC = pc;
        return 0;
    }


    switch (input[0]) {
        case 'g':
            cpu->PC = strtol(&input[1], NULL, 16);
            return -1;
        case 'n':
            // next instruction
            break;
        case 'x':
            count = strtol(&input[1], NULL, 16);
            addr = strtol(&input[3], NULL, 16);
            printf("%02X: ", addr);
            for (int i = 0; i < count; i++) {
                printf("%02X ", cpu->RAM[addr + count]);
            }
            printf("\n");
            cpu->PC = pc;
            return -1;
        case 'b':
            addr = strtol(&input[1], NULL, 16);
            printf("breakpoint #%u: %04x\n", last_bp, addr);
            breakpoints[last_bp] = addr;
            last_bp++;
            return -1;
        case 'w':
            opcode = strtol(&input[1], NULL, 16);
            printf("watchpoint #%u: %04x\n", last_wp, opcode);
            watchpoints[last_wp] = opcode;
            last_wp++;
            return -1;
        case 'r':
            printf("running...\n");
            ignore = 1;
            break;
        case 'R':
            printf("reset CPU...\n");
            clear_screen();
            cpu_reset(cpu);
            return -1;
        case 'm':
            addr = strtol(&input[1], NULL, 16);
            count = strtol(&input[3], NULL, 16);

            if (count < 16) {
                count = 16;
            }

            for (int i = addr, c = 0; i < addr + count; i++) {
                if (c == 0) {
                    printf("%04X: ", addr + i);
                } else if (c == 7) {
                    printf(" ");
                } else if (c == 15) {
                    c = 0;
                    printf("\n");
                    continue;
                }
                printf("%02X ", cpu->RAM[i]);
                c++;
            }
            printf("\n\n");
            return -1;
        case 'q':
            printf("quitting...");
            monitor_enable = 0;
            return -1;
        default:
            cpu->PC = pc;
            return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    struct Cpu cpu;
    struct Instruction instruction;
    char *tmp = NULL;
    char program[PATH_MAX];
    monitor_enable = 0;

    if ((tmp = strrchr(argv[0], '/')) != NULL) {
        tmp ++;
        strcpy(program, tmp);
    }

    if (argc < 2) {
        printf("usage: %s ROMFILE\n", program);
        exit(1);
    }

    memset(&instruction, 0, sizeof(struct Instruction));

    srand((unsigned)time(NULL));
    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer(648, 325, SDL_WINDOW_ALLOW_HIGHDPI, &window, &renderer);
    //SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
    SDL_RenderSetScale(renderer, 10.0f, 10.0f);
    SDL_RenderPresent(renderer);

    clear_screen();
    cpu_reset(&cpu);

    size_t rom_size;
    rom_size = load_file_at(&cpu, argv[1], 0x200);
    printf("ROM size: %zu\n", rom_size);

    keyboard_state = SDL_GetKeyboardState(NULL);

    int done = 0;
    while (!done) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    done = 1;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            done = 1;
                            break;
                        case SDLK_F1:
                            monitor_enable = 1;
                            break;
                        default:
                            keyboard_buf[0] = event.key.keysym.sym;
                            break;
                    }
                default:
                    break;
            }
        }

        timer_update(&cpu.DEV[T_DELAY]);
        timer_update(&cpu.DEV[T_SND]);

        instruction = parse_data(cpu.RAM[cpu.PC] << 8 | cpu.RAM[cpu.PC + 1]);
        do_disassemble(&cpu, &instruction);
        do_operation(&cpu, &instruction);
        printf("\n");
        cpu_info(&cpu);
        printf("\n");

        if (cpu.SP > CHIP8_STACK_MAX) {
            fprintf(stderr, "FAULT: stack overflow\n");
            break;
        }

        SDL_RenderPresent(renderer);

        SDL_Delay(10);
        if (monitor_enable != 0 && monitor(&cpu) < 0) {
            continue;
        }
        keyboard_buf[0] = '\0';
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
