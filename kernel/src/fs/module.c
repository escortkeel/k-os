#include "lib/int.h"

#include "init/initcall.h"
#include "fs/module.h"
#include "fs/binfmt.h"
#include "video/log.h"

static INITCALL module_init() {
    uint32_t count = multiboot_info->mods_count;
    multiboot_module_t *mods = multiboot_info->mods;

    logf("module - detected %u module(s)", count);

    for(uint32_t i = 0; i < count; i++) {
        logf("module - #%u load %s", i + 1, binfmt_load_exe((void *) mods[i].start, mods[i].end - mods[i].start) ? "failed" : "OK");
    }

    return 0;
}

module_initcall(module_init);
