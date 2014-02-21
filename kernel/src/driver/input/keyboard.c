#include <stdbool.h>
#include <stddef.h>

#include "lib/int.h"
#include "init/initcall.h"
#include "common/asm.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "input/keyboard.h"
#include "misc/sysrq.h"
#include "video/log.h"

#define KEYBOARD_IRQ 1

#define RELEASE_BIT (1 << 7)

static bool key_state[128];
static const char key_map[] = {
          // 0x00
          0, 0,
          '\1', '\1',
          '1', '!',
          '2', '@',
          '3', '#',
          '4', '$',
          '5', '%',
          '6', '^',
          '7', '&',
          '8', '*',
          '9', '(',
          '0', ')',
          '-', '_',
          '=', '+',
          '\x7f', '\x7f', // backspace
          ' ', ' ',

          // 0x10
          'q', 'Q',
          'w', 'W',
          'e', 'E',
          'r', 'R',
          't', 'T',
          'y', 'Y',
          'u', 'U',
          'i', 'I',
          'o', 'O',
          'p', 'P',
          '[', '{',
          ']', '}',
          '\n', '\n',
            0, 0,
          'a', 'A',
          's', 'S',

          // 0x20
          'd', 'D',
          'f', 'F',
          'g', 'G',
          'h', 'H',
          'j', 'J',
          'k', 'K',
          'l', 'L',
          ';', ':',
          '\'', '\"',
          '`', '~',
          0, 0,
          '\\', '|',
          'z', 'Z',
          'x', 'X',
          'c', 'C',
          'v', 'V',

          // 0x30
          'b', 'B',
          'n', 'N',
          'm', 'M',
          ',', '<',
          '.', '>',
          '/', '?',
          0, 0,
          '*', '*',
          0, 0,
          ' ', ' ',
          0, 0,
          0, 0,
          0, 0,
          0, 0,
          0, 0,
          0, 0,

          // 0x40
          0,  0,
          0,  0,
          0,  0,
          0,  0,
          0,  0,
          0,  0,
          0,  0,
          '7', '7',
          '\3', '\3',
          '9', '9',
          '-', '-',
          '4', '4',
          '5', '5',
          '6', '6',
          '+', '+',
          '1', '1',

          // 0x50
          '\5', '\5',
          '3', '3',
          '0', '0',
          '.', '.',
          0, 0
};

static void (*keyup)(char);
static void (*keydown)(char);

void keyboard_register_key_up(void (*handler)(char)) {
    keyup = handler;
}

void keyboard_register_key_down(void (*handler)(char)) {
    keydown = handler;
}

bool shift_down() {
    return key_state[LSHIFT_KEY] || key_state[RSHIFT_KEY];
}

bool control_down() {
    return key_state[CTRL_KEY];
}

bool alt_down() {
    return key_state[ALT_KEY];
}

static char translate_code(uint16_t code) {
    bool shift = shift_down();
    if(key_state[CAPS_KEY]) {
        shift = !shift;
    }

    return key_map[code * 2 + (shift ? 1 : 0)];
}

static void dispatch(uint16_t code) {
    if(code & RELEASE_BIT) {
        code ^= RELEASE_BIT;
        key_state[code] = false;

        if(keyup) (*keyup)(translate_code(code));
    } else {
        key_state[code] = true;

        if(key_state[SYSRQ_KEY] && code != SYSRQ_KEY) {
            sysrq_handle(code);
        }

        //FIXME this is for debug
        if(key_state[CTRL_KEY] && code != CTRL_KEY) {
            sysrq_handle(code);
        }

        if(keydown) (*keydown)(translate_code(code));
    }
}

static void handle_keyboard(interrupt_t *interrupt, void *data) {
    while(inb(0x64) & 2);
    dispatch(inb(0x60));
}

#define DISABLE_PORT1 0xAD
#define DISABLE_PORT2 0xA7

#define IRQ_PORT1 (1 << 0)
#define IRQ_PORT2 (1 << 1)
#define TRANSLATION (1 << 6)

#define CLOCK_PORT1 (1 << 4)
#define CLOCK_PORT2 (1 << 5)

static INITCALL keyboard_init() {
    outb(0x64, DISABLE_PORT1);
    outb(0x64, DISABLE_PORT2);

    for(int i = 0; i < 512; i++) {
        inb(0x60);
    }

    uint8_t config = inb(0x20);
    outb(0x60, config & ~(IRQ_PORT1 | IRQ_PORT2 | TRANSLATION));

    outb(0x64, 0xAA);
    while(inb(0x60) & 1);
    while(inb(0x60) != 0x55) {
        kprintf("kbd - controlled failed BIST!");
        return 0;
    }

    bool dual_channel = config & CLOCK_PORT2;
    if(dual_channel) {
        outb(0x64, 0xA8);

        dual_channel = !(inb(0x20) & CLOCK_PORT2);
        if(dual_channel){
            outb(0x64, 0xA7);
        }
    }

    register_isr(KEYBOARD_IRQ + IRQ_OFFSET, CPL_KRNL, handle_keyboard, NULL);

    outb(0x64, 0xAB);
    while(inb(0x60) & 1);
    if(!inb(0x60)) {
        outb(0x64, 0xAE);
    }

    if(dual_channel) {
        outb(0x64, 0xA9);
        while(inb(0x60) & 1);
        if(!inb(0x60)) {
            outb(0x64, 0xA8);
        }
    }

    outb(0x60, inb(0x20) | IRQ_PORT1 | IRQ_PORT2);

    return 0;
}

subsys_initcall(keyboard_init); //FIXME create input subsystem
