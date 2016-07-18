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
        "       xor %rbp,%rbp \n"
        "       and $-16,%rsp \n"
        ".hidden _DYNAMIC \n"
        ".hidden _GLOBAL_OFFSET_TABLE_ \n"
        "       lea _DYNAMIC(%rip),%rsi \n"
        "       mov %rsi,%rdx \n"
        "       sub _GLOBAL_OFFSET_TABLE_(%rip),%rsi \n"
        "       call " START "_c \n");
