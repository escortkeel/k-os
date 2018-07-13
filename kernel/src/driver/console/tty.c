#include <stdbool.h>
#include "common/types.h"
#include "common/ringbuff.h"
#include "bug/panic.h"
#include "mm/mm.h"
#include "log/log.h"
#include "fs/type/devfs.h"
#include "driver/console/console.h"

#define BUFFLEN 60

static const char KEYMAP[] = {
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
          '\x9', '\x9',   // tab

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

typedef struct tty {
    bool keystate[128];

    file_t *console;
    ringbuff_head_t rb;
    spinlock_t lock;
} tty_t;

static char translate_keycode(tty_t *tty, uint8_t code) {
    bool capital = false;
    if(tty->keystate[LSHIFT_KEY] || tty->keystate[RSHIFT_KEY]) capital = !capital;
    if(tty->keystate[CAPS_KEY]) capital = !capital;
    return KEYMAP[code * 2 + (capital ? 1 : 0)];
}

static char handle_keystate(tty_t *tty, uint8_t code) {
    bool press = !(code & RELEASE_BIT);
    code &= ~RELEASE_BIT;
    tty->keystate[code] = press;
    return press ? translate_keycode(tty, code) : 0;
}

static ssize_t read_one_from_console(tty_t *tty, uint8_t *c) {
    return vfs_read(tty->console, c, 1);
}

#define KBD_SPECIAL_CHAR 0xE0

static void populate_rb(tty_t *tty) {
    ssize_t ret;

    uint8_t c;
    read_one_from_console(tty, &c);

    if(c == KBD_SPECIAL_CHAR) {
        char *s;

        read_one_from_console(tty, &c);
        switch(c) {
            //Arrowkey up
            case 0x48: { s = "A"; break; }
            //Arrowkey left
            case 0x4B: { s = "D"; break; }
            //Arrowkey right
            case 0x4D: { s = "C"; break; }
            //Arrowkey down
            case 0x50: { s = "B"; break; }
            //Home
            case 0x47: { s = "H"; break; }
            //End
            case 0x4F: { s = "F"; break; }
            //DEL --- FIXME supply enough info for bash to work this out
            // case 0x53: { s = "3~"; break; }
            default: { return; }
        }

        ret = ringbuff_write(&tty->rb, CSI_DOUBLE, STRLEN(CSI_DOUBLE), char);
        if(!ret) {
            panic("tty buff full");
        }
        ret = ringbuff_write(&tty->rb, s, strlen(s), char);
        if(!ret) {
            panic("tty buff full");
        }
    } else {
        c = (char) handle_keystate(tty, c);
        if(!c) {
            return;
        }
        ret = ringbuff_write(&tty->rb, &c, 1, char);
        if(!ret) {
            panic("tty buff full");
        }
    }
}

//read at most len available bytes, or block if none are avaiable
static ssize_t tty_char_read(char_device_t *cdev, char *buff, size_t len) {
    tty_t *tty = cdev->private;

    //FIXME We only ever read one scancode (plus maybe another if the scancode
    //is a special prefix) at a time.

    // There might not be any real characters to give back (i.e. handle_keystate
    // might return 0 for every char in the buffer---normally there are just a
    // few chars waiting to be processed). In this case, we just retry, with
    // vfs_read handling the blocking if neccessary.
    ssize_t ret;
    while(!(ret = ringbuff_read(&tty->rb, buff, len, char))) {
        populate_rb(tty);
    }

    return ret;
}

static ssize_t tty_char_write(char_device_t *cdev, const char *buff, size_t len) {
    tty_t *tty = cdev->private;

    //TODO move the cursor and other stuff

    return vfs_write(tty->console, buff, len);
}

static char_device_ops_t tty_ops = {
    .read = tty_char_read,
    .write = tty_char_write,
};

void tty_create(char *name) {
    devfs_publish_pending();

    path_t out;
    int32_t ret = devfs_lookup(name, &out);
    if(ret) {
        panicf("tty - lookup of console (%s) failure: %d", name, ret);
    }

    tty_t *tty = kmalloc(sizeof(tty_t));
    tty->console = vfs_open_file(out.dentry);
    memset(tty->keystate, 0, sizeof(tty->keystate));
    ringbuff_init(&tty->rb, BUFFLEN, char);
    spinlock_init(&tty->lock);

    char_device_t *cdev = char_device_alloc();
    cdev->private = tty;
    cdev->ops = &tty_ops;

    register_char_device(cdev, "tty");

    devfs_publish_pending();

    kprintf("tty - for \"%s\" created", name);
}
