#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define ENDIAN_SUFFIX "_be"
#else
#define ENDIAN_SUFFIX ""
#endif

#define LDSO_ARCH "aarch64" ENDIAN_SUFFIX

#define NO_LEGACY_INITFINI

#define TPOFF_K 16

#define REL_SYMBOLIC R_AARCH64_ABS64
#define REL_GOT R_AARCH64_GLOB_DAT
#define REL_PLT R_AARCH64_JUMP_SLOT
#define REL_RELATIVE R_AARCH64_RELATIVE
#define REL_COPY R_AARCH64_COPY
#define REL_DTPMOD R_AARCH64_TLS_DTPMOD64
#define REL_DTPOFF R_AARCH64_TLS_DTPREL64
#define REL_TPOFF R_AARCH64_TLS_TPREL64
#define REL_TLSDESC R_AARCH64_TLSDESC

// Jump to PC with ARG1 in the first argument register.
#define CRTJMP(pc, arg1)                                       \
    __asm__ __volatile__("mov x0,%1 ; br %0"                   \
                         :                                     \
                         : "r"(pc), "r"((unsigned long)(arg1)) \
                         : "memory")

// Call the C _dl_start, which returns a dl_start_return_t containing the
// user entry point and its argument.  Then jump to that entry point with
// the argument in the first argument register (x0, where it was placed by
// the C function's return), clearing the return address and frame pointer
// registers so the user entry point is the base of the call stack.
#define DL_START_ASM                                      \
    __asm__(".globl _start\n"                             \
            ".hidden _start\n"                            \
            ".type _start,%function\n"                    \
            "_start:\n"                                   \
            "    bl _dl_start\n"                          \
            "    mov x29, #0\n" /* frame pointer (FP) */  \
            "    mov x30, #0\n" /* return address (LR) */ \
            "    br x1");
