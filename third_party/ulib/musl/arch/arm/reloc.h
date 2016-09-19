#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define ENDIAN_SUFFIX "eb"
#else
#define ENDIAN_SUFFIX ""
#endif

#if __ARM_PCS_VFP
#define FP_SUFFIX "hf"
#else
#define FP_SUFFIX ""
#endif

#define LDSO_ARCH "arm" ENDIAN_SUFFIX FP_SUFFIX

#define NO_LEGACY_INITFINI

#define TPOFF_K 8

#define REL_SYMBOLIC R_ARM_ABS32
#define REL_GOT R_ARM_GLOB_DAT
#define REL_PLT R_ARM_JUMP_SLOT
#define REL_RELATIVE R_ARM_RELATIVE
#define REL_COPY R_ARM_COPY
#define REL_DTPMOD R_ARM_TLS_DTPMOD32
#define REL_DTPOFF R_ARM_TLS_DTPOFF32
#define REL_TPOFF R_ARM_TLS_TPOFF32
//#define REL_TLSDESC     R_ARM_TLS_DESC

// Jump to PC with ARG1 in the first argument register.
#define CRTJMP(pc, arg1)                                       \
    __asm__ __volatile__("mov r0,%1 ; bx %0"                   \
                         :                                     \
                         : "r"(pc), "r"((unsigned long)(arg1)) \
                         : "memory")

// This is how a C return value can be composed so it will
// deliver two words to the DL_START_ASM code, below.
typedef unsigned long long dl_start_return_t;
#define DL_START_RETURN(entry, arg) \
    (((uint64_t)(uintptr_t)(entry) << 32) | (uintptr_t)(arg))

// Call the C _dl_start, which returns a dl_start_return_t containing the
// user entry point and its argument.  Then jump to that entry point with
// the argument in the first argument register (r0, where it was placed by
// the C function's return), clearing the return address and frame pointer
// registers so the user entry point is the base of the call stack.
#define DL_START_ASM                   \
    __asm__(".globl _start\n"          \
            ".hidden _start\n"         \
            ".type _start,%function\n" \
            "_start:\n"                \
            "    bl _dl_start\n"       \
            "    mov fp, #0\n"         \
            "    mov lr, #0\n"         \
            "    bx r1");
