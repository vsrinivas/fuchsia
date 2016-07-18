/*
 * In Thumb-2 mode, pc means "here + 4".
 * In ARM mode, pc means "here + 8".
 */
#ifdef __thumb__
# define PCREL_SYM(sym) #sym "-(.Lpcrel." #sym "+4)"
#else
# define PCREL_SYM(sym) #sym "-(.Lpcrel." #sym "+8)"
#endif
#define PCREL_FIXUP(sym, reg) \
    ".Lpcrel." #sym ": add " #reg ", " #reg ", pc \n"

/*
 * Defines START(arg) to call START_c(arg, load_bias, _DYNAMIC).
 * We compute load_bias by subtracting the link-time address of
 * _DYNAMIC (conveniently stored at _GLOBAL_OFFSET_TABLE_[0] by
 * the linker) from the run-time address of _DYNAMIC, computed
 * with a PC-relative reloc and addressing mode.
 */
__asm__(".text \n"
        ".global " START " \n"
        ".type " START ",%function \n" START ": \n"
        "	mov fp, #0 \n"
        "	mov lr, #0 \n"
        "	mov ip, sp \n"
        "	and ip, ip, #-16 \n" // Align the stack.
        "	mov sp, ip \n"
        // The incoming argument is in a1 (aka r0).  Leave it there.
        ".hidden _GLOBAL_OFFSET_TABLE_\n"
        ".hidden _DYNAMIC\n"
        "	movw a2, #:lower16:" PCREL_SYM(_GLOBAL_OFFSET_TABLE_) "\n"
        "	movt a2, #:upper16:" PCREL_SYM(_GLOBAL_OFFSET_TABLE_) "\n"
        "	movw a3, #:lower16:" PCREL_SYM(_DYNAMIC) "\n"
        "	movt a3, #:upper16:" PCREL_SYM(_DYNAMIC) "\n"
        PCREL_FIXUP(_GLOBAL_OFFSET_TABLE_, a2)
        PCREL_FIXUP(_DYNAMIC, a3)
        "	ldr a2, [a2] \n"
	"	sub a2, a3, a2 \n"
        "	bl " START "_c \n");
