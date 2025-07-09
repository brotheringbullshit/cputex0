#ifndef CORTEXM0_CORE_H
#define CORTEXM0_CORE_H

#include <stdint.h>

#define M0_NUM_REGS 16
#define M0_PC 15
#define M0_SP 13
#define M0_LR 14

// CPSR Flags (simplified)
#define FLAG_N (1U << 31)
#define FLAG_Z (1U << 30)
#define FLAG_C (1U << 29)
#define FLAG_V (1U << 28)

typedef struct {
    uint32_t ctrl;    // Control register
    uint32_t reload;  // Reload value
    uint32_t current; // Current counter
} SysTick;

typedef struct {
    uint32_t regs[M0_NUM_REGS]; // R0-R15
    uint32_t cpsr;              // N,Z,C,V flags only
    SysTick systick;            // SysTick timer
} CortexM0;

typedef enum {
    M0_OK,
    M0_INVALID,
    M0_UNSUPPORTED
} M0Status;

typedef M0Status (*m0_mem_fetch_fn)(uint32_t addr, uint16_t *out_halfword);
typedef M0Status (*m0_mem_access_fn)(uint32_t addr, uint32_t *data, int write);

// Reset CPU, set PC and SP explicitly
void m0_reset(CortexM0 *cpu, uint32_t pc, uint32_t sp);

// Read vector table and init SP + PC
M0Status m0_power_on_reset(CortexM0 *cpu, m0_mem_access_fn mem);

// Trigger exception vector (push stack frame, jump handler)
M0Status m0_exception(CortexM0 *cpu, uint32_t vector_num, m0_mem_access_fn mem);

// Execute one instruction step (fetch+decode+execute)
M0Status m0_step(CortexM0 *cpu, m0_mem_fetch_fn fetch, m0_mem_access_fn mem);

// Tick SysTick timer, trigger IRQ if needed
M0Status m0_systick_tick(CortexM0 *cpu, m0_mem_access_fn mem);

#endif
