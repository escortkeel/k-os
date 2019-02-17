# K-OS [![Build Status](https://travis-ci.org/khoek/k-os.svg?branch=master)](https://travis-ci.org/khoek/k-os)

An operating system written by Keeley Hoek as a hobby. It was inspired by Charlie Somerville's [JSOS](https://github.com/charliesome/JSOS).

K-OS is a C kernel, with the exception of the required architecture-specific assembly code.

## Features

* Multiboot 1 compliant
* Memory Managment subsystem
    * Page Frame Allocator
    * Cache Allocator
    * Page Mapping Manipulation
* Drivers
    * PIT
    * PCI
    * Keyboard
    * SATA
    * PATA
    * E1000 (Intel 825xx) Ethernet Driver
* Time subsystem
    * Clock and Timer APIs
* Device subsystem
    * Unified Bus, Device and Driver APIs
* Disk/Filesystem subsystem (functional, in progress)
    * Disk label support (MSDOS, GPT)
    * Partition support (FAT32)
* Network subsystem (functional, in progress)
    * DHCP support
    * 4 layer packet switch
        * Link layer (Ethernet)
        * Network layer (IP, ARP)
        * Transport layer (TCP, UDP)
        * Application layer (ICMP, NBNS)
* Scheduler subsystem (functional, in progress)
    * Multitasking
    * System Calls (POSIX, in progress)
        * Process control (exit, fork, etc.)
        * Filesystem access (open, close, read, write, etc.)
        * Network access (socket, send, recv, etc.)
* Misc:
    * Profiler
    * SysRq support

## Compiling

The build system is handwritten, and fetches and builds the entire toolchain for you. Just run `make` in the project root directory to fetch and build everything (parallel builds with `make -j 8` are much faster and fine). The build system will prompt you when you don't have required utilities (like bison, or flex) installed or they can't be found. A burnable ISO will be emitted as (by default) `dist/image/package/cdrom.iso`.

To run/test the kernel in a QEMU virtual machine use `make run` in the project root directory (something like `apt install qemu-system-x86`, in order to provide `qemu-system-i586` is required).

### Configuration options
Currently K-OS accepts the following optional configuration options:

<table>
    <tr>
        <td>CONFIG_DEBUG_BUGCHECKS</td>
        <td>Enable system-wide bugchecks which are normally compiled into the kernel, and other debugging information.</td>
    </tr>
    <tr>
        <td>CONFIG_DEBUG_MM</td>
        <td>Enable particularly expensive MM-related bugchecks</td>
    </tr>
</table>

To enable/disable these options edit the "kernel/config.h" file.

## License

Where not explicitly indicated otherwise, all files are licensed under the following terms:

    Copyright (c) 2019, Keeley Hoek
    All rights reserved.

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:

      Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

      Redistributions in binary form must reproduce the above copyright notice, this
      list of conditions and the following disclaimer in the documentation and/or
      other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
    ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
