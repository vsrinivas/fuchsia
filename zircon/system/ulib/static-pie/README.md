# `static-pie`

A small library for applying simple fix ups to an ELF file, possibly the
currently running binary.

Binaries compiled in position-independent code (PIC) mode often require _fix
ups_ to ensure that statically initialized pointers and pointers in
compiler-generated runtime data structures point to the correct location in
memory.

This library parses the ELF file's relocation tables, applying any such fix ups.
