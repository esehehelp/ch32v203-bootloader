/*
 * CH32V203K8 bootloader image: first 8K of flash.
 * SPDX-License-Identifier: MIT
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 8K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}
INCLUDE sections.ld
