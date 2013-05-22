#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#define INITCALL int __init

#ifdef __LINKER__

#define INITCALL_SECTION(id) *(.initcall.##id)

#else

#define __init     __attribute__ ((section(".init.text"), cold))
#define __initdata __attribute__ ((section(".init.data")))
#define __initconst __attribute__ ((constsection(".init.rodata")))

typedef int (*initcall_t)(void);

#define DEFINE_INITCALL(id, fn) \
	static initcall_t __initcall_##fn##id \
	__attribute__((section(".initcall." #id), used)) = fn

#define early_initcall(fn)  DEFINE_INITCALL(0, fn)
#define pure_initcall(fn)   DEFINE_INITCALL(1, fn)
#define core_initcall(fn)   DEFINE_INITCALL(2, fn)
#define arch_initcall(fn)   DEFINE_INITCALL(3, fn)
#define subsys_initcall(fn) DEFINE_INITCALL(4, fn)
#define device_initcall(fn) DEFINE_INITCALL(5, fn)
#define module_initcall(fn) DEFINE_INITCALL(6, fn)

#endif

#endif
