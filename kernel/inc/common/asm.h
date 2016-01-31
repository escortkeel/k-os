#ifndef KERNEL_COMMON_ASM_H
#define KERNEL_COMMON_ASM_H

#include "common/types.h"

#define barrier() \
    do {                                \
        asm volatile("" ::: "memory");  \
    } while (0)

#define relax() \
    do {                                        \
        asm volatile("rep; nop" ::: "memory");  \
    } while(0)

static inline void cli() {
    __asm__ volatile("cli");
}

static inline void sti() {
    __asm__ volatile("sti");
}

static inline void hlt() {
    __asm__ volatile("hlt");
}

static inline void ltr(uint32_t idx) {
    __asm__ volatile("ltr %%ax" :: "a" (idx));
}

static inline void lgdt(volatile void *gdtr) {
    __asm__ volatile("lgdt (%0)" :: "r" (gdtr));
    barrier();
}

static inline void lidt(volatile void *idtr) {
    __asm__ volatile("lidt (%0)" :: "r" (idtr));
    barrier();
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    volatile uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port) );
}

static inline uint16_t inw(uint16_t port) {
    volatile uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port) );
}

static inline uint32_t inl(uint16_t port) {
    volatile uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static inline void insw(uint16_t port, void * buff, uint32_t size) {
    __asm__ volatile("rep insw" : "+D"(buff), "+c"(size) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, void * buff, uint32_t size) {
    __asm__ volatile("rep outsw" : "+S"(buff), "+c"(size) : "d"(port));
}

static inline void insl(uint16_t port, void * buff, uint32_t size) {
    __asm__ volatile("rep insl" : "=D" (buff), "=c" (size) : "d" (port), "0" (buff), "1" (size));
}

static inline void outsl(uint16_t port, void * buff, uint32_t size) {
    __asm__ volatile("rep outsl" : "=S" (buff), "=c" (size) : "d" (port), "0" (buff), "1" (size));
}

#endif
