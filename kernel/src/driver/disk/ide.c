#include <stdbool.h>

#include "lib/printf.h"
#include "common/asm.h"
#include "common/math.h"
#include "init/initcall.h"
#include "bug/debug.h"
#include "bug/panic.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "mm/mm.h"
#include "mm/cache.h"
#include "time/clock.h" //FIXME sleep(1) should be microseconds not hundredths of a second
#include "fs/block.h"
#include "fs/disk.h"
#include "device/device.h"
#include "driver/bus/pci.h"
#include "driver/disk/ata.h"
#include "video/log.h"

#define IDE_DEVICE_PREFIX       "hd"

#define PRIMARY_IRQ   14
#define SECONDARY_IRQ 15

#define TYPE_PATA               0x00
#define TYPE_PATAPI             0x01

#define IDE_ESUCCESS             0
#define IDE_EERROR              -1
#define IDE_EFAULT              -2
#define IDE_ENODRQ              -3

#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DF               0x20
#define ATA_SR_DSC              0x10
#define ATA_SR_DRQ              0x08
#define ATA_SR_CORR             0x04
#define ATA_SR_IDX              0x02
#define ATA_SR_ERR              0x01

#define ATA_ER_BBK              0x80
#define ATA_ER_UNC              0x40
#define ATA_ER_MC               0x20
#define ATA_ER_IDNF             0x10
#define ATA_ER_MCR              0x08
#define ATA_ER_ABRT             0x04
#define ATA_ER_TK0NF            0x02
#define ATA_ER_AMNF             0x01

#define ATA_SET_FEATURES_DMA    0x03

#define ATA_VALID_IDENT_DMA     0x02 //words 64-70
#define ATA_VALID_IDENT_UDMA    0x04 //word  88

#define ATA_TRANSFER_UDMA       0x40 //0x40 << 1 is 0x80
#define ATA_TRANSFER_DMA_MULTI  0x20 //0x20 << 1 is 0x40
#define ATA_TRANSFER_DMA_SINGLE 0x10 //0x10 << 1 is 0x20

#define ATA_IRQ_OFF             0x02
#define ATA_IRQ_ON              0x00

#define ATA_FEATURE_LBA         0x200
#define ATA_FEATURE_DMA         0x100

#define ATA_MODE_CHS            0xA0
#define ATA_MODE_LBA            0xE0

#define ATA_MASTER              0x00
#define ATA_SLAVE               0x01

#define ATA_REG_DATA            0x00
#define ATA_REG_ERROR           0x01
#define ATA_REG_FEATURES        0x01
#define ATA_REG_SECdevice0      0x02
#define ATA_REG_LBA0            0x03
#define ATA_REG_LBA1            0x04
#define ATA_REG_LBA2            0x05
#define ATA_REG_HDDEVSEL        0x06
#define ATA_REG_COMMAND         0x07
#define ATA_REG_STATUS          0x07
#define ATA_REG_SECdevice1      0x08
#define ATA_REG_LBA3            0x09
#define ATA_REG_LBA4            0x0A
#define ATA_REG_LBA5            0x0B
#define ATA_REG_CONTROL         0x0C
#define ATA_REG_ALTSTATUS       0x0C
#define ATA_REG_DEVADDRESS      0x0D
#define ATA_REG_BMCOMMAND       0x0E
#define ATA_REG_BMSTATUS        0x10
#define ATA_REG_PRDTABLE        0x12

#define ATA_BMCMD_START         0x01
#define ATA_BMCMD_READ          0x08
#define ATA_BMCMD_WRITE         0x00

#define ATA_BMSTATUS_ACTIVE     0x01
#define ATA_BMSTATUS_ERROR      0x02
#define ATA_BMSTATUS_INTERRUPT  0x04

#define ATA_PRIMARY             0x00
#define ATA_SECONDARY           0x01

#define ATA_READ                0x00
#define ATA_WRITE               0x01

#define MAX_PRD_ENTRIES         PAGE_SIZE / 8
#define PRD(address, count)     ((((((uint64_t) (count)) & 0xFFFE) << 32) | (((uint32_t) (address)) & 0xFFFFFFFE)))

typedef struct ide_channel {
    uint16_t      base;   // I/O Base.
    uint16_t      ctrl;   // Control Base
    uint16_t      bmide;  // Bus Master IDE
    page_t        *prdt;
    volatile bool irq;
    uint8_t       nIEN;   // nIEN (No Interrupt);
} ide_channel_t;

typedef struct ide_device {
    bool        present;       // 0 (Empty) or 1 (This Drive really exists).
    uint8_t     channel;       // 0 (Primary Channel) or 1 (Secondary Channel).
    uint8_t     drive;        // 0 (Master Drive) or 1 (Slave Drive).
    uint16_t    type;         // 0: ATA, 1: ATAPI.
    uint16_t    signature;     // Drive Signature
    uint16_t    features;      // Features
    bool        dma;          // DMA compatible
    uint32_t    commandSets;    // Command Sets Supported.
    uint32_t    size;         // Size in Sectors.
    char        model[41];     // Model in string.
    uint32_t    num;

    block_device_t device;
} ide_device_t;

static ide_channel_t channels[2];
static ide_device_t ide_devices[4];

static uint8_t ide_buf[2048] = {0};

static void ide_mmio_write(uint8_t channel, uint8_t reg, uint8_t data) {
    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);

    if (reg < 0x08)
        outb(channels[channel].base  + reg - 0x00, data);
    else if (reg < 0x0C)
        outb(channels[channel].base  + reg - 0x06, data);
    else if (reg < 0x0E)
        outb(channels[channel].ctrl  + reg - 0x0A, data);
    else if (reg < 0x16)
        outb(channels[channel].bmide + reg - 0x0E, data);
    else BUG();

    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

static void ide_mmio_write_long(uint8_t channel, uint8_t reg, uint32_t data) {
    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);

    if (reg < 0x08)
        outl(channels[channel].base  + reg - 0x00, data);
    else if (reg < 0x0C)
        outl(channels[channel].base  + reg - 0x06, data);
    else if (reg < 0x0E)
        outl(channels[channel].ctrl  + reg - 0x0A, data);
    else if (reg < 0x16)
        outl(channels[channel].bmide + reg - 0x0E, data);
    else BUG();

    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

static uint8_t ide_mmio_read(uint8_t channel, uint8_t reg) {
    uint8_t result;
    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);

    if (reg < 0x08)
        result = inb(channels[channel].base + reg - 0x00);
    else if (reg < 0x0C)
        result = inb(channels[channel].base  + reg - 0x06);
    else if (reg < 0x0E)
        result = inb(channels[channel].ctrl  + reg - 0x0A);
    else if (reg < 0x16)
        result = inb(channels[channel].bmide + reg - 0x0E);
    else BUG();

    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
    return result;
}

static void ide_mmio_read_buffer(uint8_t channel, uint8_t reg, void * buffer, uint32_t quads) {
    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);

    if (reg < 0x08)
        insl(channels[channel].base  + reg - 0x00, buffer, quads);
    else if (reg < 0x0C)
        insl(channels[channel].base  + reg - 0x06, buffer, quads);
    else if (reg < 0x0E)
        insl(channels[channel].ctrl  + reg - 0x0A, buffer, quads);
    else if (reg < 0x16)
        insl(channels[channel].bmide + reg - 0x0E, buffer, quads);
    else BUG();

    if (reg > 0x07 && reg < 0x0C)
        ide_mmio_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

static uint8_t ide_wait_ready(uint8_t channel) {
    uint8_t status;
    do {status = ide_mmio_read(channel, ATA_REG_STATUS);} while(status & ATA_SR_BSY);
    return status;
}

static uint8_t ide_error(uint8_t channel) {
    uint8_t status = ide_wait_ready(channel);

    if (status & ATA_SR_ERR)
        return IDE_EERROR; // Error. //TODO detect and report error code

    if (status & ATA_SR_DF)
        return IDE_EFAULT; // Device Fault.

    return IDE_ESUCCESS;
}

static uint8_t ide_poll(uint8_t channel, bool drq) {
    for(uint8_t i = 0; i < 4; i++) // Delay 400 nanosecond for BSY to be set:
        ide_mmio_read(channel, ATA_REG_ALTSTATUS); // Reading the Alternate Status port wastes 100ns; loop four times.

    uint8_t ret = ide_error(channel);
    if (ret) return ret;

    if (!drq) {
        if (!(ide_mmio_read(channel, ATA_REG_STATUS) & ATA_SR_DRQ))
        return IDE_ENODRQ;
    }

    return 0;
}

uint8_t ide_print_error(uint32_t drive, uint8_t err) {
    if (err == 0)
        return err;

    /*kprintf("IDE:");
    if (err == 1) {kprintf("- Device Fault\n       "); err = 19;}
    else if (err == 2) {
        uint8_t st = ide_mmio_read(ide_devices[drive].channel, ATA_REG_ERROR);
        if (st & ATA_ER_AMNF)    {kprintf("- No Address Mark Found\n       ");    err = 7;}
        if (st & ATA_ER_TK0NF)    {kprintf("- No Media or Media Error\n       ");    err = 3;}
        if (st & ATA_ER_ABRT)    {kprintf("- Command Aborted\n       ");        err = 20;}
        if (st & ATA_ER_MCR)    {kprintf("- No Media or Media Error\n       ");    err = 3;}
        if (st & ATA_ER_IDNF)    {kprintf("- ID mark not Found\n       ");        err = 21;}
        if (st & ATA_ER_MC)    {kprintf("- No Media or Media Error\n       ");    err = 3;}
        if (st & ATA_ER_UNC)    {kprintf("- Uncorrectable Data Error\n       ");    err = 22;}
        if (st & ATA_ER_BBK)    {kprintf("- Bad Sectors\n       ");          err = 13;}
    } else  if (err == 3)          {kprintf("- Reads Nothing\n       "); err = 23;}
       else  if (err == 4)  {kprintf("- Write Protected\n       "); err = 8;}*/

    logf("ide error - [%s %s] %s",
        (const char *[]){"Primary", "Secondary"}[ide_devices[drive].channel], // Use the channel as an index into the array
        (const char *[]){"Master", "Slave"}[ide_devices[drive].drive], // Same as above, using the drive
        ide_devices[drive].model);

    return err;
}

static void handle_irq_primary(interrupt_t *interrupt, void *data) {
    uint8_t status = ide_mmio_read(ATA_PRIMARY, ATA_REG_BMSTATUS);

    if(status & ATA_BMSTATUS_ERROR) panic("IDE - Primary Channel Data Transfer Error");

    if(status & ATA_BMSTATUS_INTERRUPT) {
        ide_mmio_write(ATA_PRIMARY, ATA_REG_BMSTATUS, ATA_BMSTATUS_INTERRUPT);

        if(status & ATA_BMSTATUS_ACTIVE) {
            panic("IDE - Primary Channel Interrupt with Active Flag Set");
        } else {
            ide_mmio_write(ATA_PRIMARY, ATA_REG_BMCOMMAND, 0);
            channels[ATA_PRIMARY].irq = true;
        }
    } else {
        panic("IDE - Primary Channel IRQ Conflict");
    }
}

static void handle_irq_secondary(interrupt_t *interrupt, void *data) {
    uint8_t status = ide_mmio_read(ATA_SECONDARY, ATA_REG_BMSTATUS);

    if(status & ATA_BMSTATUS_ERROR) panic("IDE - Secondary Channel Data Transfer Error");

    if(status & ATA_BMSTATUS_INTERRUPT) {
        ide_mmio_write(ATA_SECONDARY, ATA_REG_BMSTATUS, ATA_BMSTATUS_INTERRUPT);

        if(status & ATA_BMSTATUS_ACTIVE) {
            panic("IDE - Secondary Channel Interrupt with Active Flag Set");
        } else {
            ide_mmio_write(ATA_SECONDARY, ATA_REG_BMCOMMAND, 0);
            channels[ATA_SECONDARY].irq = true;
        }
    } else {
        panic("IDE - Secondary Channel IRQ Conflict");
    }
}

static void irq_wait(uint8_t channel) {
    while(!channels[channel].irq) hlt();
    channels[channel].irq = false;
}

static int32_t pata_access(bool write, bool same, ide_device_t *device, uint64_t numsects, uint32_t lba, void *edi) {
    uint8_t lba_mode /* 0: CHS, 1:LBA28,ide_buf 2: LBA48 */, dma /* 0: No DMA, 1: DMA */;
    uint8_t lba_io[6];
    uint32_t channel = device->channel; // Read the Channel.
    uint32_t sector_size = 512; // Almost every ATA drive has a sector-size of 512-byte. //FIXME figure this out dynamically
    uint32_t transfered = 0; //FIXME this definitley could overflow
    uint16_t cyl, i;
    uint8_t head, sect, err;

    if (!write && same) panic("IDE - Cannot read same!");

        // (I) Select one from LBA28, LBA48 or CHS;
    if (lba >= 0x10000000) { // Sure Drive should support LBA in this case, or you are
                         // giving a wrong LBA.
        // LBA48:
        lba_mode  = 2;
        lba_io[0] = (lba & 0x000000FF) >> 0;
        lba_io[1] = (lba & 0x0000FF00) >> 8;
        lba_io[2] = (lba & 0x00FF0000) >> 16;
        lba_io[3] = (lba & 0xFF000000) >> 24;
        lba_io[4] = 0; // LBA28 is integer, so 32-bits are enough to access 2TB.
        lba_io[5] = 0; // LBA28 is integer, so 32-bits are enough to access 2TB.
        head        = 0; // Lower 4-bits of HDDEVSEL are not used here.
    } else if (device->features & ATA_FEATURE_LBA)  { // Drive supports LBA?
        // LBA28:
        lba_mode  = 1;
        lba_io[0] = (lba & 0x00000FF) >> 0;
        lba_io[1] = (lba & 0x000FF00) >> 8;
        lba_io[2] = (lba & 0x0FF0000) >> 16;
        lba_io[3] = 0; // These Registers are not used here.
        lba_io[4] = 0; // These Registers are not used here.
        lba_io[5] = 0; // These Registers are not used here.
        head        = (lba & 0xF000000) >> 24;
    } else {
        // CHS:
        lba_mode  = 0;
        sect        = (lba % 63) + 1;
        cyl          = (lba + 1  - sect) / (16 * 63);
        lba_io[0] = sect;
        lba_io[1] = (cyl >> 0) & 0xFF;
        lba_io[2] = (cyl >> 8) & 0xFF;
        lba_io[3] = 0;
        lba_io[4] = 0;
        lba_io[5] = 0;
        head        = (lba + 1  - sect) % (16 * 63) / (63); // Head number is written to HDDEVSEL lower 4-bits.
    }

    // (II) See if drive supports DMA or not;
    dma = false;

    if (lba_mode > 0 /* There is no CHS (lba_mode == 0) for DMA */ && device->features & ATA_FEATURE_DMA) {
        dma = true;

        edi = virt_to_phys(edi); //Translate to physical address

        ide_mmio_write(channel, ATA_REG_CONTROL, channels[channel].nIEN = ATA_IRQ_ON);
        ide_mmio_write_long(channel, ATA_REG_PRDTABLE, (uint32_t) page_to_phys(channels[channel].prdt)); //store the address of the PRDT

        uint64_t *prdt = (uint64_t *) page_to_virt(channels[channel].prdt);
        uint64_t bytes = sector_size * (numsects == 0 ? 512 : numsects);
        uint64_t i;
        for (i = 0; i < MAX_PRD_ENTRIES && bytes > (64 * 1024); i++) {
            *prdt++ = PRD(edi + (same ? 0 : ((64 * 1024) * i)), 0 /* 64kb */);
            transfered += 64 * 1024;//FIXME increment and return this after the operation completes WITHOUT AN ERROR
            bytes -= (64 * 1024);
        }

        if (bytes > 0 && i < MAX_PRD_ENTRIES) {
            bytes = MIN(bytes, 64 * 1024);
            transfered += bytes;

            *prdt++ = PRD(edi + (same ? 0 : ((64 * 1024) * i)), bytes == (64 * 1024) ? 0 : bytes);
        }

        *(prdt - 1) |= 0x8000000000000000; // mark last PRD entry as EOT
    } else {
        ide_mmio_write(channel, ATA_REG_CONTROL, channels[channel].nIEN = ATA_IRQ_OFF);
    }

    // (III) Wait if the drive is busy;
    ide_wait_ready(channel);

    // (IV) Select Drive from the controller, along with CHS/LBA;
    ide_mmio_write(channel, ATA_REG_HDDEVSEL, (lba_mode ? ATA_MODE_LBA : ATA_MODE_CHS) | (((uint32_t) device->drive) << 4) | head);

    // (V) Write Parameters;
    if (lba_mode == 2) {
        ide_mmio_write(channel, ATA_REG_SECdevice1,    0);
        ide_mmio_write(channel, ATA_REG_LBA3,    lba_io[3]);
        ide_mmio_write(channel, ATA_REG_LBA4,    lba_io[4]);
        ide_mmio_write(channel, ATA_REG_LBA5,    lba_io[5]);
    }

    ide_mmio_write(channel, ATA_REG_SECdevice0, numsects > 255 ? 0 : numsects);
    ide_mmio_write(channel, ATA_REG_LBA0,    lba_io[0]);
    ide_mmio_write(channel, ATA_REG_LBA1,    lba_io[1]);
    ide_mmio_write(channel, ATA_REG_LBA2,    lba_io[2]);

    // (VI) Select the command and send it;
    // Routine that is followed:
    // If ( DMA & LBA48)    DO_DMA_EXT;
    // If ( DMA & LBA28)    DO_DMA_LBA;
    // If ( DMA & LBA28)    DO_PIO_CHS; - There is no DMA for CHS
    // If (!DMA & LBA48)    DO_PIO_EXT;
    // If (!DMA & LBA28)    DO_PIO_LBA;
    // If (!DMA & !LBA#)    DO_PIO_CHS;

    if (lba_mode == 0 && !dma && !write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_PIO_READ);
    if (lba_mode == 1 && !dma && !write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_PIO_READ);
    if (lba_mode == 2 && !dma && !write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_PIO_READ_EXT);
    if (lba_mode == 0 &&  dma && !write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_DMA_READ);
    if (lba_mode == 1 &&  dma && !write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_DMA_READ);
    if (lba_mode == 2 &&  dma && !write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_DMA_READ_EXT);
    if (lba_mode == 0 && !dma &&  write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_PIO_WRITE);
    if (lba_mode == 1 && !dma &&  write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_PIO_WRITE);
    if (lba_mode == 2 && !dma &&  write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_PIO_WRITE_EXT);
    if (lba_mode == 0 &&  dma &&  write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_DMA_WRITE);
    if (lba_mode == 1 &&  dma &&  write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_DMA_WRITE);
    if (lba_mode == 2 &&  dma &&  write) ide_mmio_write(channel, ATA_REG_COMMAND, ATA_CMD_DMA_WRITE_EXT);

    if (dma) {
        ide_mmio_write(channel, ATA_REG_BMCOMMAND, ATA_BMCMD_START | (write ? ATA_BMCMD_WRITE : ATA_BMCMD_READ));
        irq_wait(channel);
    } else {
        if (numsects > 255) {
            numsects = 0; // restrict the size of PIO read/writes to 256 sectors at a time, for performance.
        }

        if (!write) {
        for (i = 0; i < (numsects == 0 ? 256 : numsects); i++) {
            if ((err = ide_poll(channel, 1))) {
                return err; // Polling, set error and exit if there is. FIXME Make errors negative
            }

            asm("rep insw" : : "c"(sector_size / 2 /* in words */), "d"(channels[channel].base /* bus base */), "D"(edi)); // Receive Data.
            transfered +=  sector_size;
            edi += sector_size;
        }
        } else {
        // PIO Write.
        for (i = 0; i < (numsects == 0 ? 256 : numsects); i++) {
            ide_poll(channel, 0); // Polling.
            asm("rep outsw"::"c"(sector_size / 2 /* in words */), "d"(channels[channel].base /* bus base */), "S"(edi)); // Send Data
            transfered += sector_size;

            if (!same) edi += sector_size;
        }

        ide_mmio_write(channel, ATA_REG_COMMAND, lba_mode == 2 ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
        ide_poll(channel, 0); // Polling. FIXME Detect and report error
        }
    }

    return transfered / sector_size;
}

static int32_t ide_read_sectors(ide_device_t *device, uint64_t numsects, uint32_t lba, void *edi) {
    if((lba + numsects) > device->size) return 0;

    if (device->type == TYPE_PATA) {
        return pata_access(ATA_READ, false, device, numsects, lba, edi);
    } else if (device->type == TYPE_PATAPI) {
        //TODO implement ATAPI
        return 0;
    } else {
        panic("IDE - unknown device type");
    }
}

static int32_t ide_write_sectors(ide_device_t *device, uint64_t numsects, uint32_t lba, void *edi) {
    if((lba + numsects) > device->size) return 0;

    if (device->type == TYPE_PATA) {
        return pata_access(ATA_WRITE, false, device, numsects, lba, edi);
    } else if (device->type == TYPE_PATAPI) {
        //TODO implement ATAPI
        return 0;
    } else {
        panic("IDE - unknown device type");
    }
}

static ssize_t ide_read(block_device_t *device, void *buff, size_t start, size_t blocks) {
    return ide_read_sectors(containerof(device, ide_device_t, device), blocks, start, buff);
}

static ssize_t ide_write(block_device_t *device, void *buff, size_t start, size_t blocks) {
    return ide_write_sectors(containerof(device, ide_device_t, device), blocks, start, buff);
}

static block_device_ops_t ide_device_ops = {
    .read = ide_read,
    .write = ide_write,
};

static char * ide_controller_name(device_t UNUSED(*device)) {
    static int next_id = 0;

    char *name = kmalloc(STRLEN(IDE_DEVICE_PREFIX) + STRLEN(STR(MAX_UINT)) + 1);
    sprintf(name, "%s%u", IDE_DEVICE_PREFIX, next_id++);

    return name;
}

static bool ide_probe(device_t UNUSED(*device)) {
    return true;
}

static void ide_enable(device_t *device) {
    pci_device_t *pci_device = containerof(device, pci_device_t, device);

    //FIXME this is VERY hacky
    static bool once = false;
    if(once) return;
    once = true;

    register_isr(PRIMARY_IRQ + IRQ_OFFSET, CPL_KERNEL, handle_irq_primary, NULL);
    register_isr(SECONDARY_IRQ + IRQ_OFFSET, CPL_KERNEL, handle_irq_secondary, NULL);

    // 1- Detect I/O Ports which interface IDE Controller:
    channels[ATA_PRIMARY  ].base  = (BAR_ADDR_32(pci_device->bar[0])) + 0x1F0 * (!pci_device->bar[0]);
    channels[ATA_PRIMARY  ].ctrl  = (BAR_ADDR_32(pci_device->bar[1])) + 0x3F4 * (!pci_device->bar[1]);
    channels[ATA_PRIMARY  ].bmide = BAR_ADDR_32((pci_device->bar[4])) + 0; // Bus Master IDE
    channels[ATA_PRIMARY  ].prdt  = alloc_page(0); //FIXME? page is never freed, reserves entire page for PRDT

    channels[ATA_SECONDARY].base  = BAR_ADDR_32(pci_device->bar[2]) + 0x170 * (!pci_device->bar[2]);
    channels[ATA_SECONDARY].ctrl  = BAR_ADDR_32(pci_device->bar[3]) + 0x374 * (!pci_device->bar[3]);
    channels[ATA_SECONDARY].bmide = BAR_ADDR_32(pci_device->bar[4]) + 8; // Bus Master IDE
    channels[ATA_SECONDARY].prdt  = alloc_page(0); //FIXME? page is never freed, reserves entire page for PRDT

    // 2- Disable IRQs:
    ide_mmio_write(ATA_PRIMARY  , ATA_REG_CONTROL, ATA_IRQ_OFF);
    ide_mmio_write(ATA_SECONDARY, ATA_REG_CONTROL, ATA_IRQ_OFF);

    // 3- Detect ATA-ATAPI Devices:
    int d = 0;
    for (uint8_t i = 0; i < 2; i++) {
        for (uint8_t j = 0; j < 2; j++) {
            uint8_t err = 0, type = TYPE_PATA, status;
            ide_devices[d].present = false; // Assuming that no drive here.

            // (I) Select Drive:
            ide_mmio_write(i, ATA_REG_HDDEVSEL, 0xA0 | (j << 4)); // Select Drive.
            sleep(1); // Wait 1ms for drive select to work.

            // (II) Send ATA Identify Command:
            ide_mmio_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
            sleep(1); // This function should be implemented in your OS. which waits for 1 ms.
                     // it is based on System Timer Device Driver.

            // (III) Polling:
            if (ide_mmio_read(i, ATA_REG_STATUS) == 0) continue; // If Status = 0, No Device.

            while(1) {
                status = ide_mmio_read(i, ATA_REG_STATUS);
                if ((status & ATA_SR_ERR)) {err = 1; break;} // If Err, Device is not ATA.
                if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break; // Everything is right.
            }

            // (IV) Probe for ATAPI Devices:
            if (err != 0) {
                uint8_t cl = ide_mmio_read(i, ATA_REG_LBA1);
                uint8_t ch = ide_mmio_read(i, ATA_REG_LBA2);

                if (cl == 0x14 && ch == 0xEB)
                    type = TYPE_PATAPI;
                else if (cl == 0x69 && ch == 0x96)
                    type = TYPE_PATAPI;
                else
                    continue; // Unknown Type (may not be a device).

                ide_mmio_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
                sleep(1);
            }

            // (V) Read Identification Space of the Device:
            ide_mmio_read_buffer(i, ATA_REG_DATA, ide_buf, 128);

            // (VI) Read Device Parameters:
            ide_devices[d].present     = true;
            ide_devices[d].type        = type;
            ide_devices[d].channel     = i;
            ide_devices[d].drive       = j;
            ide_devices[d].signature   = *((uint16_t *) (ide_buf + ATA_IDENT_DEVICETYPE));
            ide_devices[d].features    = *((uint16_t *) (ide_buf + ATA_IDENT_FEATURES));
            ide_devices[d].commandSets = *((uint32_t *) (ide_buf + ATA_IDENT_COMMANDSETS));

            //FIXME does this work? (autodecteting the best hdd speeds and setting them)

            int8_t mode = -1;
            if (ide_devices[d].features & ATA_FEATURE_DMA) {
                if ((((uint16_t *) ide_buf)[ATA_IDENT_VALID_IDENT]) & ATA_VALID_IDENT_UDMA) {
                    uint8_t supported = ((uint16_t *) ide_buf)[ATA_IDENT_UDMA];
                    for (mode = 5; mode >= 0; mode--) {
                        if (!(supported & (1 << mode))) continue;

                        ide_mmio_write(i, ATA_REG_SECdevice0, ATA_TRANSFER_UDMA + (mode & 0x7));
                        ide_mmio_write(i, ATA_REG_FEATURES, ATA_SET_FEATURES_DMA);
                        ide_mmio_write(i, ATA_REG_COMMAND, ATA_CMD_SET_FEATURES);

                        if(!ide_error(i)) {
                            break;
                        }
                    }
                }

                if(mode < 0) {
                    if((((uint16_t *) ide_buf)[ATA_IDENT_VALID_IDENT]) & ATA_VALID_IDENT_DMA) {
                        uint8_t supported = ((uint16_t *) ide_buf)[ATA_IDENT_DMA_MULTI];
                        for (mode = 2; mode >= 0; mode--) {
                            if (!(supported & (1 << mode))) continue;

                            ide_mmio_write(i, ATA_REG_SECdevice0, ATA_TRANSFER_DMA_MULTI + (mode & 0x7));
                            ide_mmio_write(i, ATA_REG_FEATURES, ATA_SET_FEATURES_DMA);
                            ide_mmio_write(i, ATA_REG_COMMAND, ATA_CMD_SET_FEATURES);

                            if(!ide_error(i)) {
                                break;
                            }
                        }
                    }
                }

                if(mode < 0) {
                    if((((uint16_t *) ide_buf)[ATA_IDENT_VALID_IDENT]) & ATA_VALID_IDENT_DMA) {
                        uint8_t supported = ((uint16_t *) ide_buf)[ATA_IDENT_DMA_SINGLE];
                        for (mode = 2; mode >= 0; mode--) {
                            if (!(supported & (1 << mode))) continue;

                            ide_mmio_write(i, ATA_REG_SECdevice0, ATA_TRANSFER_DMA_SINGLE + (mode & 0x7));
                            ide_mmio_write(i, ATA_REG_FEATURES, ATA_SET_FEATURES_DMA);
                            ide_mmio_write(i, ATA_REG_COMMAND, ATA_CMD_SET_FEATURES);

                            if(!ide_error(i)) {
                                break;
                            }
                        }
                    }
                }
            }

            //TODO optimise PIO

            // (VII) Get Size:
            if (ide_devices[d].commandSets & (1 << 26)) {
                // Device uses 48-Bit Addressing:
                ide_devices[d].size = *((uint32_t *)(ide_buf + ATA_IDENT_MAX_LBA_EXT));
            } else {
                // Device uses CHS or 28-bit Addressing:
                ide_devices[d].size = *((uint32_t *)(ide_buf + ATA_IDENT_MAX_LBA));
            }

            // Model string is swapped, unswap it
            for(uint8_t k = 0; k < 40; k += 2) {
                ide_devices[d].model[k] = ide_buf[ATA_IDENT_MODEL + k + 1];
                ide_devices[d].model[k + 1] = ide_buf[ATA_IDENT_MODEL + k];
            }

            ide_devices[d].num = d;
            ide_devices[d].model[40] = 0; // Terminate String.

            ide_devices[d].device.ops = &ide_device_ops;
            ide_devices[d].device.size = ide_devices[d].size / 512;
            ide_devices[d].device.block_size = 512;

            logf("ide - %s %s %7uMB", (const char *[]){"PATA  ", "PATAPI"}[ide_devices[i].type], ide_devices[d].model, ide_devices[i].size / 1024 / 2);

            char *name = kmalloc(STRLEN(IDE_DEVICE_PREFIX) + 2);
            memcpy(name, IDE_DEVICE_PREFIX, STRLEN(IDE_DEVICE_PREFIX));
            name[STRLEN(IDE_DEVICE_PREFIX)] = 'a' + d;
            name[STRLEN(IDE_DEVICE_PREFIX) + 1] = '\0';

            register_block_device(&ide_devices[d].device, name);
            register_disk(&ide_devices[d].device);

            d++;
        }
    }
}

static void ide_disable(device_t UNUSED(*device)) {
    //TODO iterate through ide_disks[] and call unregister_disk (once implemented)
}

static void ide_destroy(device_t UNUSED(*device)) {
}

static pci_ident_t ide_idents[] = {
    {
        .vendor =     PCI_ID_ANY,
        .device =     PCI_ID_ANY,
        .class  =     0x01010000,
        .class_mask = 0xFFFF0000
    }
};

static pci_driver_t ide_driver = {
    .driver = {
        .bus = &pci_bus,

        .name = ide_controller_name,
        .probe = ide_probe,

        .enable = ide_enable,
        .disable = ide_disable,
        .destroy = ide_destroy
    },

    .supported = ide_idents,
    .supported_len = sizeof(ide_idents) / sizeof(pci_ident_t)
};

static INITCALL ide_init() {
    register_driver(&ide_driver.driver);

    return 0;
}

postcore_initcall(ide_init);
