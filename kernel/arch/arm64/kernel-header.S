// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


// Optional header included in start.S needed to make fastboot happy
_fastboot_start:
    // magic instruction that gives us UEFI "MZ" signature
    add x13, x18, #0x16
    b header_end

    .quad   KERNEL_LOAD_OFFSET
    .quad   __data_end - _start
    .quad   0

    .quad   0
    .quad   0
    .quad   0

    .byte   'A'
    .byte   'R'
    .byte   'M'
    .byte   0x64
    .long   pe_header - _start
    .align 3

pe_header:
    .ascii  "PE"                            //PE Magic
    .short  0                               //  PE Magic

    // coff header
    .short  0xaa64                          //Machine
    .short  2                               //NumberOfSections
    .long   0                               //TimeDateStamp
    .long   0                               //PointerToSymbolTable
    .long   1                               //NumberOfSymbols
    .short  section_table - optional_header //SizeOfOptionalHeader
    .short  0x206                           //Characteristics

optional_header:
    .short  0x20B                           //signature
    .byte   0x2                             //MajorLinkerVersion
    .byte   0x14                            //MinorLinkerVersion
    .long   _end - header_end               //sizeOfCode
    .long   0                               //SizeOfInitializedData
    .long   0                               //SizeofUninitializedData
    .long   header_end - _start             //AdressOfEntryPoint
    .long   header_end - _start             //BaseOfCode

    .quad   0                               //ImageBase
    .long   0x1000                          //SectionAlignment
    .long   0x200                           //FileAlignment
    .short  0                               //MajorOSVersion
    .short  0                               //MinorOSVersion
    .short  0                               //MajorImageVersion
    .short  0                               //MinorImageVersion
    .short  0                               //MajorSubsystemVersion
    .short  0                               //MinorSubsystemVersion
    .long   0                               //Win32VersionValue

    .long   _end - _start                   //SizeOfImage
    .long   header_end - _start             //SizeOfHeaders
    .long   0                               //Checksum
    .short  0xA                             //Subsystem
    .short  0                               //DLLCharacteristics
    .quad   0                               //SizeOfStackReserve
    .quad   0                               //SizeOfStackCommit
    .quad   0                               //SizeOfHeapReserve
    .quad   0                               //SizeOfHeapCommit
    .long   0                               //LoaderFlags
    .long   6                               //NumberOfRvaAndSizes
    .quad   0                               //DataDirectory[6]
    .quad   0
    .quad   0
    .quad   0
    .quad   0
    .quad   0

section_table:
    .ascii  ".reloc"
    .byte   0
    .byte   0
    .long   0
    .long   0
    .long   0
    .long   0
    .long   0
    .long   0
    .short  0
    .short  0
    .long   0x42100040

    .ascii  ".text"                         //Name[6] (char)
    .byte   0
    .byte   0
    .byte   0
    .long   0                               //PhysicalAddress/VirtualSize
    .long   header_end - _start             //VirtualAddress
    .long   __data_end - header_end         //SizeOfRawData
    .long   header_end - _start             //PointerToRawData
    .long   0                               //PointertoRelocations
    .long   0                               //PointerToLinenumbers
    .short  0                               //NumberOfRelocations
    .short  0                               //NumberOfLinenumbers
    .long   0xE0500020                      //Characteristics
    .align 12

header_end:
    // fall through to arm_reset
