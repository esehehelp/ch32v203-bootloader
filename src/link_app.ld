/*
 * CH32V203K8 application image: flash after the 8K bootloader.
 * SPDX-License-Identifier: MIT
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00002000, LENGTH = 56K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}
INCLUDE sections.ld
