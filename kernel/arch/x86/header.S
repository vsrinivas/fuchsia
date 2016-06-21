// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <asm.h>
#include <arch/x86/asm.h>

/* The magic number for the Multiboot header. */
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002

/* The flags for the Multiboot header. */
#define MULTIBOOT_HEADER_FLAGS 0x00010002

.section ".text.header", "ax"
.code16

.align 4

FUNCTION(_setup_start)

    /* screen_info             0x000-0x040 struct screen_info */
    .org 0x040
    /* apm_bios_info           0x040-0x054 struct apm_bios_info */
    .org 0x054
    /* padding */
    .org 0x058
    /* tboot_addr              0x058-0x060 */
    .org 0x060
    /* ist_info                0x060-0x070 struct ist_info */
    .org 0x070
    /* padding */
    .org 0x080
    /* hd0_info                0x080-0x090 (obsolete) */
    .org 0x090
    /* hd1_info                0x090-0x0A0 (obsolete) */
    .org 0x0A0
    /* sys_desc_table          0x0A0-0x0B0 struct sys_desc_table */
    .org 0x0B0
    /* olpc_ofw_header         0x0B0-0x0C0 */
    .org 0x0C0
    /* ext_ramdisk_image       0x0C0-0x0C4 */
    .org 0x0C4
    /* ext_ramdisk_size        0x0C4-0x0C8 */
    .org 0x0C8
    /* ext_cmd_line_ptr        0x0C8-0x0CC */
    .org 0x0CC
    /* padding */
    .org 0x140
    /* edid_info               0x140-0x1C0 struct edid_info */
    .org 0x1C0
    /* efi_info                0x1C0-0x1E0 struct efi_info */
    .org 0x1E0
    /* alk_mem_k               0x1E0-0x1E4 */
    .org 0x1E4
    /* scratch                 0x1E4-0x1E8 */
    .org 0x1E8
    /* e820_entries            0x1E8-0x1E9 */
e820_entries:
    .byte 0
    /* eddbuf_entries          0x1E9-0x1EA */
    .org 0x1EA
    /* edd_mbr_sig_buf_entries 0x1EA-0x1EB */
    .org 0x1EB
    /* kbd_status              0x1EB-0x1EC */
    .org 0x1EC
    /* padding */
    .org 0x1EF
    /* sentinel                0x1EF-0x1F0 */
    .org 0x1F0
    /* padding */
    .org 0x1F1
    /* hdr                     0x1F1-0x268 */
        /* setup_sects */
        .byte (((_setup_end - _setup_start) >> 9) - 1)
        /* root_flags */
        .short 0
        /* syssize */
        .int _zero_page_sys_size
        /* ram_size */
        .short 0
        /* vid_mode */
        .short 0
        /* root_dev */
        .short 0
        /* boot_flag */
        .short 0xAA55
        /* jump */
        .byte 0xEB, end_of_setup_header - 0f
0:
        /* header */
        .ascii "HdrS"
        /* version */
        .short 0x020C
        /* realmode_swtch */
        .int 0
        /* start_sys (obsolete) */
        .short 0x1000
        /* kernel_version */
        .short 0
        /* type_of_loader */
        .byte 0
        /* loadflags */
        .byte 0x01
        /* setup_move_size */
        .short 0
        /* code32_start */
        .int PHYS(_protected_start)
        /* ramdisk_image */
        .int 0
        /* ramdisk_size */
        .int 0
        /* bootsect_kludge (obsolete) */
        .int 0
        /* heap_end_ptr */
        .short 0
        /* ext_loader_ver */
        .byte 0
        /* ext_loader_type */
        .byte 0
        /* cmd_line_ptr */
        .int 0
        /* initrd_addr_max */
        .int 0x7fffffff
        /* kernel_alignment */
        .int 0x200000
        /* relocatable_kernel */
        .byte 0
        /* min_alignment */
        .byte 8
        /* xloadflags */
        .short 0
        /* cmdline_size */
        .int 2048
        /* hardware_subarch */
        .int 0
        /* hardware_subarch_data */
        .quad 0
        /* payload_offset */
        .int 0
        /* payload_length */
        .int 0
        /* setup_data */
        .quad 0
        /* pref_address */
        .long PHYS(_protected_start)
        .long 0
        /* init_size */
        .long 0
        /* handover_offset */
        .long 0
end_of_setup_header:
    jmp end_of_zero_page
0:
    /* padding */
    .org 0x290
    /* edd_mbr_sig_buffer      0x290-0x2D0 */
    .org 0x2D0
    /* e820_map                0x2D0-0xCD0 */
e820_map:
    .org 0xCD0
    /* padding */
    .org 0xD00
    /* eddbuf                  0xD00-0xEEC */
    .org 0xEEC
    /* padding */
    .org 0x1000

end_of_zero_page:
    /* gather e820 information if it wasn't provided */
    cmpw $0, (e820_entries - _setup_start)
    jne e820_loop_end

    /* %ebx is the "continuation value" and should start out as zero */
    mov $0, %ebx
    /* set %es:%edi to point at the e820 map */
    mov $(e820_map - _setup_start), %edi

e820_loop:

    /* request the e820 function of int 15 */
    mov $0xe820, %eax
    /* write at most 20 bytes of data at %es:%edi */
    mov $20, %ecx
    /* 'SMAP' to prove we're really asking for e820 data */
    mov $0x534d4150, %edx

    /* call the interrupt routine */
    int $0x15

    /* if carry was set, that's an error or passing the end of the map */
    jc e820_loop_end
    /* if %eax is not now equal to SMAP, then this BIOS doesn't support e820 */
    cmp $0x534d4150, %eax
    jne e820_loop_end

    /* if no data was written, skip this entry */
    jecxz e820_loop
    /* if the length of the entry is zero, skip it */
    mov %es:8(%edi), %eax
    or %es:12(%edi), %eax
    cmp $0, %eax
    je e820_loop

    /* increment the entry count and the map pointer */
    incb (e820_entries - _setup_start)
    add %ecx, %edi

    /* if %ebx is now zero again, we just read the last entry */
    cmp $0, %ebx
    je e820_loop_end

    /* go back around and request the next entry */
    jmp e820_loop
e820_loop_end:


    /* load a temporary GDT to help us get into protected mode */
    lgdtl (_setup_gdtr - _setup_start)

    /* enable protected mode */
    mov %cr0, %eax
    btsl $0, %eax
    mov %eax, %cr0

    /* use a long jump to load the protected mode code descriptor */
    ljmpl $0x08,$0f
0:

    .code32

    /* clear out the real mode segment state */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %ss
    movw %ax, %gs
    movw %ax, %ss

    /* %esi should point to the start of the boot_params structure */
    movl $_setup_start, %esi

    /* jump into the protected mode part of the kernel */
    jmp PHYS(_protected_start)

    /* an abridged copy of the gdt for use in real mode */
.align 8
_setup_gdtr:
    .short _setup_gdt_end - _setup_gdt - 1
    .int _setup_gdt

.align 8
_setup_gdt:
    .int 0
    .int 0

    /* 32 bit CPL 0 code segment */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00         /* base 23:16 */
    .byte  0b10011010       /* P(1) DPL(00) S(1) 1 C(0) R(1) A(0) */
    .byte  0b11001111       /* G(1) D(1) 0 0 limit 19:16 */
    .byte  0x0          /* base 31:24 */

    /* 32 bit CPL 0 data segment */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00         /* base 23:16 */
    .byte  0b10010010       /* P(1) DPL(00) S(1) 0 E(0) W(1) A(0) */
    .byte  0b11001111       /* G(1) B(1) 0 0 limit 19:16 */
    .byte  0x0          /* base 31:24 */

_setup_gdt_end:

.align 8
.type _multiboot_header,STT_OBJECT
_multiboot_header:
    /* magic */
    .int MULTIBOOT_HEADER_MAGIC
    /* flags */
    .int MULTIBOOT_HEADER_FLAGS
    /* checksum */
    .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

    /* header_addr */
    .int PHYS_LOAD_ADDRESS - PHYS_HEADER_LOAD_OFFSET + (_multiboot_header - _setup_start)
    /* load_addr */
    .int PHYS_LOAD_ADDRESS - PHYS_HEADER_LOAD_OFFSET
    /* load_end_addr */
    .int PHYS(__data_end)
    /* bss_end_addr */
    .int PHYS(__bss_end)
    /* entry_addr */
    .int PHYS(_multiboot_start)

.align 4096

_setup_end:
