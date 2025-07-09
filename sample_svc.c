// svc sample test (THERES NO SYSCALL TABLE ALSO!!!!!!!!)
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
    memory[addr + 3] = (val >> 24);
}

void setup_vectors() {
    write_word(0x00, 0x20001000); // Initial SP
    write_word(0x04, 0x00001000); // Reset handler
    write_word(0x2C, 0x00002000); // SVC (#11) handler
}

void load_program() {
    // Program at 0x1000:
    // MOV R0, #42
    // SVC #0xAB
    // ADD R0, #1
    // B . (loop)
    uint16_t code[] = {
        0x202A, // MOV R0, #42
        0xDFAB, // SVC #0xAB
        0x3001, // ADD R0, #1
        0xE7FD  // B .
    };
    memcpy(&memory[0x1000], code, sizeof(code));
}

void load_svc_handler() {
    // Handler at 0x2000:
    // MOV R1, #0xEF
    // LSLS R1, R1, #8
    // ADD R1, #0xBE
    // B .
    uint16_t handler[] = {
        0x21EF, // MOV R1, #0xEF
        0x0149, // LSLS R1, R1, #5 (EF << 5 = 0x1DE0)
        0x31BE, // ADD R1, #0xBE → Final R1 = (EF << 5) + BE
        0xE7FD  // B .
    };
    memcpy(&memory[0x2000], handler, sizeof(handler));
}

int main() {
    setup_vectors();
    load_program();
    load_svc_handler();

    if (m0_power_on_reset(&cpu, mem_access) != M0_OK) {
        printf("Failed to reset CPU\n");
        return 1;
    }

    for (int i = 0; i < 20; i++) {
        if (m0_step(&cpu, fetch_halfword, mem_access) != M0_OK) {
            printf("Exception occurred\n");
            break;
        }

        printf("Step %2d: PC=%08X R0=%08X R1=%08X  N=%d Z=%d\n", i,
               cpu.regs[M0_PC],
               cpu.regs[0], cpu.regs[1],
               !!(cpu.cpsr & FLAG_N), !!(cpu.cpsr & FLAG_Z));
    }

    return 0;
}
