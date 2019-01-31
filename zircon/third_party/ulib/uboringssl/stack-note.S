// This is equivalent to the --noexecstack flag.
// Without this, a bare assembly file can be presumed to require
// executable stack and cause a PT_GNU_STACK program header that says so.
#if defined(__ASSEMBLER__) && defined(__ELF__)
.section .note.GNU-stack,""
#endif
