#ifndef KERNEL_ARCH_PIC_H
#define KERNEL_ARCH_PIC_H

#include "init/initcall.h"

#define PIC_NUM_PINS 8

#define PIC_MASTER_OFFSET IRQ_OFFSET
#define PIC_SLAVE_OFFSET  (PIC_MASTER_OFFSET + PIC_NUM_PINS)

#define IRQ_SPURIOUS (PIC_NUM_PINS - 1)
#define INT_SPURIOUS_MASTER (PIC_MASTER_OFFSET + IRQ_SPURIOUS)
#define INT_SPURIOUS_SLAVE  (PIC_SLAVE_OFFSET + IRQ_SPURIOUS)

void __init pic_configure(uint8_t master_mask, uint8_t slave_mask);
void __init pic_init();

#endif
