#include "cortexm0_core.h"
#include <stdio.h>

#define VECTOR_ADDR(n) ((n) * 4)

static inline void set_pc(CortexM0 *cpu, uint32_t val) {
    cpu->regs[M0_PC] = val & ~1; // Halfword aligned
}

static inline uint32_t get_pc(CortexM0 *cpu) {
    return cpu->regs[M0_PC];
}

static inline void set_flags(CortexM0 *cpu, uint32_t result) {
    if (result == 0) cpu->cpsr |= FLAG_Z; else cpu->cpsr &= ~FLAG_Z;
    if (result & 0x80000000) cpu->cpsr |= FLAG_N; else cpu->cpsr &= ~FLAG_N;
}

static inline void set_add_flags(CortexM0 *cpu, uint32_t a, uint32_t b, uint32_t result) {
    set_flags(cpu, result);
    cpu->cpsr &= ~(FLAG_C | FLAG_V);
    if (result < a || result < b) cpu->cpsr |= FLAG_C;
    if (((a ^ ~b) & (a ^ result)) & 0x80000000) cpu->cpsr |= FLAG_V;
}

static inline void set_sub_flags(CortexM0 *cpu, uint32_t a, uint32_t b, uint32_t result) {
    set_flags(cpu, result);
    cpu->cpsr &= ~(FLAG_C | FLAG_V);
    if (a >= b) cpu->cpsr |= FLAG_C;
    if (((a ^ b) & (a ^ result)) & 0x80000000) cpu->cpsr |= FLAG_V;
}

static M0Status read32(uint32_t addr, uint32_t *val, m0_mem_access_fn mem) {
    return mem(addr, val, 0);
}

void m0_reset(CortexM0 *cpu, uint32_t pc, uint32_t sp) {
    for (int i = 0; i < M0_NUM_REGS; ++i) cpu->regs[i] = 0;
    cpu->regs[M0_PC] = pc;
    cpu->regs[M0_SP] = sp;
    cpu->cpsr = 0;
    cpu->systick.ctrl = 0;
    cpu->systick.reload = 0;
    cpu->systick.current = 0;
}

M0Status m0_power_on_reset(CortexM0 *cpu, m0_mem_access_fn mem) {
    uint32_t sp_init, pc_init;
    if (read32(0x00000000, &sp_init, mem) != M0_OK) return M0_INVALID;
    if (read32(0x00000004, &pc_init, mem) != M0_OK) return M0_INVALID;
    m0_reset(cpu, pc_init, sp_init);
    return M0_OK;
}

M0Status m0_exception(CortexM0 *cpu, uint32_t vector_num, m0_mem_access_fn mem) {
    uint32_t vector_addr = VECTOR_ADDR(vector_num);
    uint32_t handler;
    if (read32(vector_addr, &handler, mem) != M0_OK)
        return M0_INVALID;

    // Push simplified exception frame (dummy values except LR)
    uint32_t sp = cpu->regs[M0_SP];
    for (int i = 7; i >= 0; --i) {
        uint32_t val = (i == 6) ? cpu->regs[M0_LR] : 0xDEADBEEF;
        sp -= 4;
        if (mem(sp, &val, 1) != M0_OK) return M0_INVALID;
    }
    cpu->regs[M0_SP] = sp;

    set_pc(cpu, handler);
    return M0_OK;
}

M0Status m0_step(CortexM0 *cpu, m0_mem_fetch_fn fetch, m0_mem_access_fn mem) {
    uint16_t op;
    uint32_t pc = get_pc(cpu);

    if (fetch(pc, &op) != M0_OK)
        return m0_exception(cpu, 3, mem); // HardFault

    set_pc(cpu, pc + 2);

    if ((op & 0xF800) == 0x1800) { // ADD Rd, Rs, Rn
        uint8_t rn = (op >> 6) & 0x7, rs = (op >> 3) & 0x7, rd = op & 0x7;
        uint32_t res = cpu->regs[rn] + cpu->regs[rs];
        set_add_flags(cpu, cpu->regs[rn], cpu->regs[rs], res);
        cpu->regs[rd] = res;
        return M0_OK;
    }

    if ((op & 0xF800) == 0x1C00) { // SUB Rd, Rs, Rn
        uint8_t rn = (op >> 6) & 0x7, rs = (op >> 3) & 0x7, rd = op & 0x7;
        uint32_t res = cpu->regs[rn] - cpu->regs[rs];
        set_sub_flags(cpu, cpu->regs[rn], cpu->regs[rs], res);
        cpu->regs[rd] = res;
        return M0_OK;
    }

    if ((op & 0xF800) == 0x2000) { // MOV Rd, #imm
        uint8_t rd = (op >> 8) & 0x7, imm = op & 0xFF;
        cpu->regs[rd] = imm;
        set_flags(cpu, imm);
        return M0_OK;
    }

    if ((op & 0xF800) == 0x3000) { // ADD Rd, #imm
        uint8_t rd = (op >> 8) & 0x7, imm = op & 0xFF;
        uint32_t res = cpu->regs[rd] + imm;
        set_add_flags(cpu, cpu->regs[rd], imm, res);
        cpu->regs[rd] = res;
        return M0_OK;
    }

    if ((op & 0xF800) == 0x3800) { // SUB Rd, #imm
        uint8_t rd = (op >> 8) & 0x7, imm = op & 0xFF;
        uint32_t res = cpu->regs[rd] - imm;
        set_sub_flags(cpu, cpu->regs[rd], imm, res);
        cpu->regs[rd] = res;
        return M0_OK;
    }

    if ((op & 0xF800) == 0x6000) { // STR Rd, [Rn, Rm]
        uint8_t rm = (op >> 6) & 0x7, rn = (op >> 3) & 0x7, rd = op & 0x7;
        uint32_t addr = cpu->regs[rn] + cpu->regs[rm];
        uint32_t val = cpu->regs[rd];
        return mem(addr, &val, 1);
    }

    if ((op & 0xF800) == 0x6800) { // LDR Rd, [Rn, Rm]
        uint8_t rm = (op >> 6) & 0x7, rn = (op >> 3) & 0x7, rd = op & 0x7;
        uint32_t addr = cpu->regs[rn] + cpu->regs[rm];
        uint32_t val = 0;
        M0Status st = mem(addr, &val, 0);
        if (st != M0_OK) return m0_exception(cpu, 3, mem);
        cpu->regs[rd] = val;
        set_flags(cpu, val);
        return M0_OK;
    }

    if ((op & 0xFF00) == 0xB400) { // PUSH {Rlist}
        uint8_t list = op & 0xFF;
        uint32_t sp = cpu->regs[M0_SP];
        for (int i = 7; i >= 0; i--) {
            if (list & (1 << i)) {
                sp -= 4;
                uint32_t val = cpu->regs[i];
                if (mem(sp, &val, 1) != M0_OK) return m0_exception(cpu, 3, mem);
            }
        }
        cpu->regs[M0_SP] = sp;
        return M0_OK;
    }

    if ((op & 0xFF00) == 0xBC00) { // POP {Rlist}
        uint8_t list = op & 0xFF;
        uint32_t sp = cpu->regs[M0_SP];
        for (int i = 0; i < 8; i++) {
            if (list & (1 << i)) {
                uint32_t val = 0;
                if (mem(sp, &val, 0) != M0_OK) return m0_exception(cpu, 3, mem);
                cpu->regs[i] = val;
                sp += 4;
            }
        }
        cpu->regs[M0_SP] = sp;
        return M0_OK;
    }

    if ((op & 0xFF00) == 0xDF00) { // SVC #imm
    uint8_t imm = op & 0xFF;
    (void)imm; // optional: you could store it in a register or handler
    return m0_exception(cpu, 11, mem); // Exception vector 11 is SVC
    }

    // Unknown opcode — trigger HardFault
    return m0_exception(cpu, 3, mem);
}

M0Status m0_systick_tick(CortexM0 *cpu, m0_mem_access_fn mem) {
    if (!(cpu->systick.ctrl & 1)) return M0_OK; // Disabled

    if (cpu->systick.current == 0) {
        cpu->systick.current = cpu->systick.reload;
        return m0_exception(cpu, 15, mem); // Vector #15 IRQ
    } else {
        cpu->systick.current--;
    }
    return M0_OK;
}
