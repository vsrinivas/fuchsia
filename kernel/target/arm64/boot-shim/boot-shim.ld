/* Copyright 2018 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

ENTRY(_start)
SECTIONS
{
    . = 0;

    .text.boot0 : {
        *(.text.boot0)
    } :load
    .text : {
        *(.text*)
        *(.gnu.linkonce.t.*)
    }

    .rodata : {
        *(.rodata* .gnu.linkonce.r.*)
    }

    .data : {
        *(.data .data.* .gnu.linkonce.d.*)
    }

    .bss : {
        *(.bss*) *(.sbss*) *(COMMON)
    }

    /*
     * Pad the shim image out to the alignment the kernel requires.
     * A complete arm64 ZBI can be appended directly to the shim.
     */
    PROVIDE_HIDDEN(embedded_zbi = ALIGN(KERNEL_ALIGN));

    /*
     * We need some nonzero contents to prevent the linker from treating
     * this as part of bss, i.e. not actually in the image.  The magic
     * number is 'BOOTSHIM' in little-endian, but nothing should care.
     */
    .pad embedded_zbi - 8 : {
	QUAD(0x4d494853544f4f42);
        ASSERT(ABSOLUTE(.) == ABSOLUTE(embedded_zbi), "shim size check");
    }
}

PHDRS
{
    load PT_LOAD FLAGS(7); /* PF_R|PF_W|PF_X */
}
