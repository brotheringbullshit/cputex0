// sample code for th elibrary
#include "cortexm0_core.h"
#include <stdio.h>
#include <string.h>

#define MEM_SIZE 0x10000
uint8_t memory[MEM_SIZE];
CortexM0 cpu;

M0Status mem_access(uint32_t addr, uint32_t *data, int write) {
    if (addr + 3 >= MEM_SIZE) return M0_INVALID;
    if (write) {
        for (int i = 0; i < 4; ++i)
            memory[addr + i] = (*data >> (8 * i)) & 0xFF;
    } else {
        *data = memory[addr] |
                (memory[addr + 1] << 8) |
                (memory[addr + 2] << 16) |
                (memory[addr + 3] << 24);
    }
    return M0_OK;
}

M0Status fetch_halfword(uint32_t addr, uint16_t *out) {
    if (addr + 1 >= MEM_SIZE) return M0_INVALID;
    *out = memory[addr] | (memory[addr + 1] << 8);
    return M0_OK;
}

void write_word(uint32_t addr, uint32_t val) {
    memory[addr]     = val & 0xFF;
    memory[addr + 1] = (val >> 8) & 0xFF;
    memory[addr + 2] = (val >> 16) & 0xFF;
    memory[addr + 3] = (val >> 24) & 0xFF;
}

void setup_vectors() {
    write_word(0x00, 0x20001000); // Initial SP
    write_word(0x04, 0x00001000); // Reset handler (program)
    write_word(0x3C, 0x00002000); // Exception #15 (SysTick IRQ)
}

void load_program() {
    // Very simple Thumb-1 code:
    // MOV R0, #0x00
    // ADD R0, #1
    // B .-2 (infinite loop)
    uint16_t program[] = {
        0x2000, // MOV R0, #0
        0x3001, // ADD R0, #1
        0xE7FD  // B .-2 (loop back)
    };
    memcpy(&memory[0x1000], program, sizeof(program));
}

void load_irq_handler() {
    // IRQ #15 at 0x2000 (SysTick handler):
    // MOV R1, #0xFF
    // B .-2 (infinite loop)
    uint16_t handler[] = {
        0x21FF, // MOV R1, #0xFF
        0xE7FD  // B .-2
    };
    memcpy(&memory[0x2000], handler, sizeof(handler));
}

int main() {
    setup_vectors();
    load_program();
    load_irq_handler();

    // Init CPU from vector table
    if (m0_power_on_reset(&cpu, mem_access) != M0_OK) {
        printf("Failed to reset CPU\n");
        return 1;
    }

    // Enable SysTick: fires every 3 steps
    cpu.systick.ctrl = 1;
    cpu.systick.reload = 3;
    cpu.systick.current = 3;

    // Run main loop
    for (int i = 0; i < 20; i++) {
        if (m0_step(&cpu, fetch_halfword, mem_access) != M0_OK) {
            printf("Exception occurred\n");
        }
        m0_systick_tick(&cpu, mem_access);

        printf("Step %2d: PC=%08X R0=%08X R1=%08X Flags: N=%d Z=%d\n", i,
            cpu.regs[M0_PC],
            cpu.regs[0],
            cpu.regs[1],
            (cpu.cpsr & FLAG_N) != 0,
            (cpu.cpsr & FLAG_Z) != 0
        );
    }

    return 0;
}
