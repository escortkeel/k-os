#include "lib/string.h"
#include "lib/printf.h"
#include "common/compiler.h"
#include "bug/panic.h"
#include "sync/spinlock.h"
#include "arch/cpu.h"
#include "time/clock.h"
#include "driver/console/console.h"

#define LOG_BUFF_SIZE 32768
#define TIME_BUFF_SIZE 64
#define LINE_BUFF_SIZE 1024

static uint32_t front;
static char log_buff[LOG_BUFF_SIZE];
static DEFINE_SPINLOCK(log_lock);

static char timestamp_buff[TIME_BUFF_SIZE];
static char line_buff[LINE_BUFF_SIZE];
static DEFINE_SPINLOCK(line_buff_lock);
static bool enabled = true;

void vlog_disable() {
    enabled = false;
}

void vlog_enable() {
    enabled = true;
}

static void logbuff_append(const char *str, uint32_t len) {
    uint32_t coppied = 0;
    while(coppied < len) {
        uint32_t chunk_len = MIN(len - coppied, LOG_BUFF_SIZE - front);

        memcpy(log_buff + front, str, chunk_len);

        if(con_global && enabled) {
            vram_write(con_global, str, len);
        }

        coppied += chunk_len;
        front = (front + chunk_len) % LOG_BUFF_SIZE;
    }
}

static void log(const char *str, uint32_t len) {
    uint32_t time = uptime();

    uint32_t flags;
    spin_lock_irqsave(&log_lock, &flags);

    uint32_t timestamp_len = sprintf(timestamp_buff, "[%5u.%03u] ", time / MILLIS_PER_SEC, time % MILLIS_PER_SEC);

    logbuff_append("\n", 1);
    logbuff_append(timestamp_buff, timestamp_len);
    logbuff_append(str, len);

    spin_unlock_irqstore(&log_lock, flags);
}

void kprint(const char *str) {
    log(str, strlen(str));
}

void kprintf(const char *fmt, ...) {
    uint32_t flags;
    spin_lock_irqsave(&line_buff_lock, &flags);

    va_list va;
    va_start(va, fmt);
    uint32_t len = vsprintf(line_buff, fmt, va);
    va_end(va);

    log(line_buff, len);

    spin_unlock_irqstore(&line_buff_lock, flags);
}
