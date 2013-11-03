.global flush_segment_registers
.global flush_tss
.global get_eflags

.extern set_kernel_stack
.extern task_usermode

.type flush_segment_registers, @function
flush_segment_registers:
    jmp  $0x08, $flush_data_segments
.size flush_segment_registers, .-flush_segment_registers

.type flush_data_segments, @function
flush_data_segments:
    movl $0x10, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ret
.size flush_data_segments, .-flush_data_segments

.type get_eflags, @function
get_eflags:
    pushf
    pop %eax
    ret
.size get_eflags, .-get_eflags
