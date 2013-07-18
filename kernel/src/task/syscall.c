#include <stddef.h>

#include "lib/string.h"
#include "common/init.h"
#include "common/asm.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "bug/panic.h"
#include "mm/cache.h"
#include "time/timer.h"
#include "time/clock.h"
#include "task/task.h"
#include "net/socket.h"
#include "video/log.h"

#define MAX_SYSCALL 256

static inline void * param_ptr(interrupt_t *interrupt, uint32_t num) {
    return (void *) *((uint32_t *) (interrupt->cpu.exec.esp + (num * sizeof(uint32_t))));
}

static inline uint32_t param_32(interrupt_t *interrupt, uint32_t num) {
    return *((uint32_t *) (interrupt->cpu.exec.esp + (num * sizeof(uint32_t))));
}

static inline uint16_t param_16(interrupt_t *interrupt, uint32_t num) {
    return (uint16_t) *((uint32_t *) (interrupt->cpu.exec.esp + (num * sizeof(uint32_t))));
}

static inline uint8_t param_8(interrupt_t *interrupt, uint32_t num) {
    return (uint8_t) *((uint32_t *) (interrupt->cpu.exec.esp + (num * sizeof(uint32_t))));
}

typedef void (*syscall_t)(interrupt_t *);

static void sys_exit(interrupt_t *interrupt) {
    logf("sys_exit: %d", interrupt->cpu.reg.ecx);

    task_exit(current, interrupt->cpu.reg.ecx);
}

static void sys_fork(interrupt_t *interrupt) {
    current->ret = ~interrupt->cpu.reg.ecx;
}

static void wake_task(task_t *task) {
    task_wake(task);
}

static void sys_sleep(interrupt_t *interrupt) {
    task_sleep(current);
    timer_create(interrupt->cpu.reg.ecx, (void (*)(void *)) wake_task, current);
    task_reschedule();
}

static void sys_log(interrupt_t *interrupt) {
    if(interrupt->cpu.reg.edx > 1023) {
        logf("syscall - task log string too long!");
    } else {
        char *buff = kmalloc(interrupt->cpu.reg.edx + 1);
        memcpy(buff, (void *) interrupt->cpu.reg.ecx, interrupt->cpu.reg.edx);
        buff[interrupt->cpu.reg.edx] = '\0';

        log(buff);

        kfree(buff, interrupt->cpu.reg.edx + 1);
    }
}

static void sys_uptime(interrupt_t *interrupt) {
    current->ret = uptime();
}

static void sys_socket(interrupt_t *interrupt) {
    gfd_idx_t fd = sock_create_fd(interrupt->cpu.reg.ecx, interrupt->cpu.reg.edx, interrupt->cpu.reg.ebx);

    current->ret = fd == FD_INVALID ? -1 : ufdt_add(current, 0, fd);
}

static void sys_send(interrupt_t *interrupt) {
    gfd_idx_t fd = ufd_to_gfd(current, interrupt->cpu.reg.ecx);

    if(fd == FD_INVALID) current->ret = -1;
    else {
        //TODO sanitize buffer argument

        sock_t *sock = gfd_to_sock(fd);
        current->ret = sock_send(sock, (void *) interrupt->cpu.reg.edx, interrupt->cpu.reg.ebx, interrupt->cpu.reg.esi);
    }
}

static syscall_t syscalls[MAX_SYSCALL] = {
    [0] = sys_exit,
    [1] = sys_fork,
    [2] = sys_sleep,
    [3] = sys_log,
    [4] = sys_uptime,
    [5] = sys_socket,
    [6] = sys_send,
};

static void syscall_handler(interrupt_t *interrupt) {
    if(interrupt->cpu.reg.eax >= MAX_SYSCALL || syscalls[interrupt->cpu.reg.eax] == NULL) {
        panicf("Unregistered Syscall #%u: 0x%X", interrupt->cpu.reg.eax, interrupt->cpu.reg.ecx);
    } else {
        syscalls[interrupt->cpu.reg.eax](interrupt);
    }
}

static INITCALL syscall_init() {
    idt_register(0x80, CPL_USER, syscall_handler);

    return 0;
}

core_initcall(syscall_init);
