/*
 * Defines START(arg) to call START_c(arg, load_bias, _DYNAMIC).
 * We compute load_bias by subtracting the link-time address of
 * _DYNAMIC (conveniently stored at _GLOBAL_OFFSET_TABLE_[0] by
 * the linker) from the run-time address of _DYNAMIC, computed
 * with a PC-relative reloc and addressing mode.
 */
__asm__(".text \n"
        ".global " START "\n"
        ".type " START ",%function\n" START ":\n"
        "       mov x29, #0\n" // frame pointer (FP)
        "       mov x30, #0\n" // return address (LR)
        "       mov x16, sp\n"
        "       and sp, x16, #-16\n" // align the stack
        // The incoming argument is in x0.  Leave it there as first argument.
        ".hidden _GLOBAL_OFFSET_TABLE_\n"
        ".hidden _DYNAMIC\n"
        "       adrp x1, _GLOBAL_OFFSET_TABLE_\n"
        "       adrp x2, _DYNAMIC\n"
        "       ldr x1, [x1, #:lo12:_GLOBAL_OFFSET_TABLE_]\n"
        "       add x2, x2, #:lo12:_DYNAMIC\n"
        "       sub x1, x2, x1\n"
        "       b " START "_c\n");
