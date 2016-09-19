#define LDSO_ARCH "x86_64"

#define REL_SYMBOLIC R_X86_64_64
#define REL_OFFSET32 R_X86_64_PC32
#define REL_GOT R_X86_64_GLOB_DAT
#define REL_PLT R_X86_64_JUMP_SLOT
#define REL_RELATIVE R_X86_64_RELATIVE
#define REL_COPY R_X86_64_COPY
#define REL_DTPMOD R_X86_64_DTPMOD64
#define REL_DTPOFF R_X86_64_DTPOFF64
#define REL_TPOFF R_X86_64_TPOFF64
#define REL_TLSDESC R_X86_64_TLSDESC

// Jump to PC with ARG1 in the first argument register.
#define CRTJMP(pc, arg1)                                       \
    __asm__ __volatile__("jmp *%0"                             \
                         :                                     \
                         : "r"(pc), "D"((unsigned long)(arg1)) \
                         : "memory")

#define GETFUNCSYM(fp, sym, got)          \
    __asm__(".hidden " #sym "\n"          \
            "	lea " #sym "(%%rip),%0\n" \
            : "=r"(*fp)                   \
            :                             \
            : "memory")

// Call the C _dl_start, which returns a dl_start_return_t containing the
// user entry point and its argument.  Then jump to that entry point with
// the argument in the first argument register, pushing a zero return
// address and clearing the frame pointer register so the user entry point
// is the base of the call stack.
#define DL_START_ASM                   \
    __asm__(".globl _start\n"          \
            ".hidden _start\n"         \
            ".type _start,%function\n" \
            "_start:\n"                \
            "    call _dl_start\n"     \
            "    and $-16,%rsp\n"      \
            "    xor %rbp,%rbp\n"      \
            "    mov %rax,%rdi\n"      \
            "    push %rbp\n"          \
            "    jmp *%rdx");
