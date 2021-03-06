#include "common/types.h"
#include "init/initcall.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/proc.h"
#include "mm/mm.h"
#include "mm/cache.h"
#include "sched/proc.h"
#include "sched/sched.h"
#include "log/log.h"

processor_t *bsp;
static DEFINE_LIST(procs);
DEFINE_PER_CPU(processor_t *, this_proc);

processor_t * register_proc(uint32_t num) {
    processor_t *proc = kmalloc(sizeof(processor_t));
    proc->num = num;
    proc->percpu_data = num ? map_page(page_to_phys(alloc_pages(DIV_UP(((uint32_t) &percpu_data_end) - ((uint32_t) &percpu_data_start), PAGE_SIZE), 0))) : &percpu_data_start;
    list_add(&proc->list, &procs);

    arch_setup_proc(proc);

    get_percpu(this_proc) = proc;

    return proc;
}

static void management_interrupt(interrupt_t *interrupt, void *data) {
    check_irqs_disabled();

    if(panic_in_progress) {
        die();
    }
}

void dispatch_management_interrupts() {
    if(!tasking_up) {
        return;
    }

    processor_t *proc;
    LIST_FOR_EACH_ENTRY(proc, &procs, list) {
        send_management_interrupt(proc);
    }
}

static INITCALL proc_init() {
    bsp = register_proc(BSP_ID);
    register_isr(MANAGEMENT_INT, CPL_KRNL, management_interrupt, NULL);

    return 0;
}

arch_initcall(proc_init);
