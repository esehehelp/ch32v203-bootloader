/*
 * CH32V203K8 shared section layout. MEMORY comes from link_app.ld /
 * link_boot.ld, which INCLUDE this file.
 * SPDX-License-Identifier: MIT
 *
 * The last 16 bytes of RAM are left out of every image: the word at
 * _boot_magic survives a system reset and tells the bootloader to stay.
 */

ENTRY(_start)
__stack_size = 2048;
PROVIDE(_boot_magic = ORIGIN(RAM) + LENGTH(RAM) - 4);

SECTIONS
{
    .init : {
        _sinit = .;
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
        _einit = .;
    } >FLASH AT>FLASH

    .vector : {
        KEEP(*(.vector));
        . = ALIGN(64);
    } >FLASH AT>FLASH

    .text : {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata*)
        *(.gnu.linkonce.t.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
    } >FLASH AT>FLASH

    .fini : {
        KEEP(*(SORT_NONE(.fini)))
        . = ALIGN(4);
    } >FLASH AT>FLASH

    PROVIDE(_etext = .);

    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH AT>FLASH

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o) .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH AT>FLASH

    .dalign : {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
    } >RAM AT>FLASH

    .dlalign : {
        . = ALIGN(4);
        PROVIDE(_data_lma = .);
    } >FLASH AT>FLASH

    .data : {
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
        *(.sdata2.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        *(.srodata.cst16)
        *(.srodata.cst8)
        *(.srodata.cst4)
        *(.srodata.cst2)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        PROVIDE(_edata = .);
    } >RAM AT>FLASH

    .bss : {
        . = ALIGN(4);
        PROVIDE(_sbss = .);
        *(.sbss .sbss.*)
        *(.gnu.linkonce.sb.*)
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON*)
        . = ALIGN(4);
        PROVIDE(_ebss = .);
    } >RAM AT>FLASH

    PROVIDE(_end = _ebss);

    .stack ORIGIN(RAM) + LENGTH(RAM) - 16 - __stack_size : {
        PROVIDE(_heap_end = .);
        . = ALIGN(4);
        PROVIDE(_susrstack = .);
        . = . + __stack_size;
        PROVIDE(_eusrstack = .);
    } >RAM
}
