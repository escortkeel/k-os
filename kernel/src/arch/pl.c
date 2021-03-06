#include "common/types.h"
#include "arch/pl.h"
#include "arch/gdt.h"
#include "arch/cpu.h"
#include "arch/proc.h"
#include "bug/debug.h"
#include "log/log.h"
#include "sched/task.h"

void enter_syscall() {
    check_irqs_disabled();

    barrier();
    irqenable();
}

void leave_syscall() {
    barrier();
    irqdisable();
}

void save_stack(void *sp) {
    check_irqs_disabled();

    BUG_ON(!current);
    BUG_ON(current->arch.live_state);

    current->arch.live_state = sp;
}

#define ALIGN_BITS 16

void switch_stack(thread_t *old, thread_t *next, void (*callback)(thread_t *old, thread_t *next)) {
    check_irqs_disabled();

    uint32_t new_esp = ((((uint32_t) next->arch.live_state) / ALIGN_BITS) - 1) * ALIGN_BITS;

    asm volatile("mov %[new_esp], %%esp\n"
                 "push %[next]\n"
                 "push %[old]\n"
                 "call *%[finish]"
        :
        : [new_esp] "r" (new_esp),
          [old] "r" (old),
          [next] "r" (next),
          [finish] "r" (callback)
        : "memory");

    BUG();
}

extern void do_context_switch(uint32_t cr3, cpu_state_t *live_state);

void context_switch(thread_t *t) {
    check_irqs_disabled();
    check_on_correct_stack();

    BUG_ON(!t);
    BUG_ON(!t->arch.live_state);

    tss_set_stack(t->kernel_stack_bottom);

    barrier();

    void *s = t->arch.live_state;
    t->arch.live_state = NULL;

    barrier();

    do_context_switch(t->arch.cr3, s);

    BUG();
}

#define kernel_stack_off(task, off) ((void *) (((uint32_t) (task)->kernel_stack_bottom) - (off)))

static inline void set_exec_state(exec_state_t *exec, void *ip, bool user) {
    exec->eip = (uint32_t) ip;
    exec->eflags = EFLAGS_IF;
    if(user) {
        exec->cs = SEL_USER_CODE | SPL_USER;
    } else {
        exec->cs = SEL_KRNL_CODE | SPL_KRNL;
    }
}

static inline void set_stack_state(stack_state_t *stack, void *sp, bool user) {
    if(user) {
        stack->ss = SEL_USER_DATA | SPL_USER;
        stack->esp = (uint32_t) sp;
    } else {
        //For kernel->kernel irets, ss and esp are not popped off (or pushed in
        //the first place).
        BUG();
    }
}

//If src==NULL we will just fill the registers with zeroes.
static inline void set_reg_state(registers_t *dst, registers_t *src) {
    if(src) {
        memcpy(dst, src, sizeof(registers_t));
    } else {
        memset(dst, 0, sizeof(registers_t));
    }
}

static inline void set_live_state(thread_t *t, void *live_state) {
    BUG_ON(!live_state);
    t->arch.live_state = live_state;
}

//If reg==NULL we will just fill the registers with zeroes.
void pl_enter_userland(void *user_ip, void *user_stack, registers_t *reg) {
    thread_t *me = current;

    //We are about to build a register launchpad on the stack to jumpstart
    //execution at the user address.
    cpu_state_t user_state;

    //Build the register launchpad in user_state.
    set_reg_state(&user_state.reg, reg);
    set_exec_state(&user_state.exec, user_ip, true);
    set_stack_state(&user_state.stack, user_stack, true);

    //We can't be interrupted after clobbering live_state, else a task switch
    //interrupt will modify this value and we will end up who-knows-where?
    irqdisable();

    //Remove our the "kernel thread" flag
    me->flags &= ~THREAD_FLAG_KERNEL;

    //Inform context_switch() of the register launchpad's location.
    set_live_state(me, &user_state);

    //Begin execution at user_ip (after some final chores).
    context_switch(me);
}

void pl_bootstrap_userland(void *user_ip, void *user_stack, uint32_t argc,
    void *argv, void *envp) {
    registers_t reg;
    memset(&reg, 0, sizeof(registers_t));
    reg.eax = argc;
    reg.ebx = (uint32_t) argv;
    reg.ecx = (uint32_t) envp;

    pl_enter_userland(user_ip, user_stack, &reg);
}

#define stack_alloc(stack, type) ({(stack) -= (sizeof(type)); ((type *) (stack));})

void pl_setup_thread(thread_t *t, void *ip, void *arg) {
    //It is absolutely imperative that "t" is not running, else the live_state
    //we install may be overwritten due to an interrupt.
    BUG_ON(t->state != THREAD_BUILDING);

    void *stack = t->kernel_stack_bottom;

    //Ensure 16 byte alignment of stack frame. I don't know why the argument is
    //8 byte aligned.
    stack_alloc(stack, void *);
    stack_alloc(stack, void *);
    *stack_alloc(stack, void *) = arg;
    stack_alloc(stack, void *);

    //Allocate space for the register launchpad on the new kernel stack.
    cpu_launchpad_t *state = stack_alloc(stack, cpu_launchpad_t);

    //Build the register launchpad.
    set_reg_state(&state->reg, NULL);
    set_exec_state(&state->exec, ip, false);

    //Inform context_switch() of the register launchpad's location.
    set_live_state(t, state);
}

//replaces page dir with new passed one, or creates new one if newdir is NULL
void * arch_replace_mem(thread_t *t, void *newdir) {
    void *olddir = t->arch.dir;

    page_t *page;
    if(newdir) {
        page = virt_to_page(newdir);
    } else {
        page = alloc_page(ALLOC_ZERO);
        build_page_dir(page_to_virt(page));
    }

    t->arch.dir = page_to_virt(page);
    t->arch.cr3 = (uint32_t) page_to_phys(page);

    //FIXME is this a hack?
    if(t == current) {
        loadcr3(t->arch.cr3);
    }

    return olddir;
}

void arch_free_mem(void *dir) {
    //FIXME free page directory dir
}

void arch_thread_build(thread_t *t) {
    arch_replace_mem(t, NULL);
}

typedef struct fork_data {
    cpu_state_t resume_state;
} fork_data_t;

void arch_ret_from_fork(void *arg) {
    irqdisable();

    fork_data_t forkd;
    memcpy(&forkd, arg, sizeof(fork_data_t));
    kfree(arg);

    forkd.resume_state.reg.edx = 0;
    forkd.resume_state.reg.eax = 0;

    pl_enter_userland((void *) forkd.resume_state.exec.eip, (void *) forkd.resume_state.stack.esp, &forkd.resume_state.reg);
}

void * arch_prepare_fork(cpu_state_t *state) {
    fork_data_t *forkd = kmalloc(sizeof(fork_data_t));
    memcpy(&forkd->resume_state, state, sizeof(cpu_state_t));
    return forkd;
}

typedef struct sigdata {
    uint32_t restore_mask;
    cpu_state_t state;
} PACKED sigdata_t;

void arch_setup_sigaction(cpu_state_t *state, void *sighandler, void *sigtramp,
    uint32_t restore_mask) {
    //assert that we are not modifying a kernel state
    BUG_ON(state->exec.cs != (SEL_USER_CODE | SPL_USER));

    uint32_t sp = state->stack.esp;

    //4b align sigdata
    sp -= sizeof(sigdata_t);
    sp &= ~0xF;
    sigdata_t *sd = (void *) sp;
    sd->restore_mask = restore_mask;
    sd->state = *state;

    //16b align esp + 4 on entry to the sighandler, then put on the arg to
    //sigtramp (sd) and then the address of sigtramp, in that order.
    sp -= sizeof(uint32_t) * 2;
    sp &= ~0xFF;
    ((uint32_t *) sp)[1] = (uint32_t) sd; //I have no idea why this offset works
    ((uint32_t *) sp)[-1] = (uint32_t) sigtramp;
    sp -= sizeof(uint32_t);

    //sp is now positioned at the top of the stack, and we are ready to go
    state->stack.esp = sp;
    state->exec.eip = (uint32_t) sighandler;
}

void arch_setup_sigreturn(cpu_state_t *state, void *sp) {
    sigdata_t *sd = sp;

    //assert that we are not modifying a kernel state
    BUG_ON(state->exec.cs != (SEL_USER_CODE | SPL_USER));

    current->node->sig_mask = sd->restore_mask;
    *state = sd->state;

    //Prevent privilege escalation
    state->exec.eflags &= EFLAGS_USER_MASK;
    state->exec.eflags |= EFLAGS_IF;
    state->exec.cs = SEL_USER_CODE | SPL_USER;
    state->stack.ss = SEL_USER_DATA | SPL_USER;
}
