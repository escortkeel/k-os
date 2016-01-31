#ifndef KERNEL_LIB_RAND_H
#define KERNEL_LIB_RAND_H

#include "common/types.h"

void srand(uint32_t seed);
uint8_t rand8();
uint16_t rand16();
uint32_t rand32();

#endif
