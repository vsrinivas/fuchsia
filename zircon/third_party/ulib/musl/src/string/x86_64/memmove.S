#include "asm.h"

ENTRY(memmove)
	mov %rdi,%rax
	sub %rsi,%rax
	cmp %rdx,%rax
.hidden __memcpy_fwd
	jae __memcpy_fwd
	mov %rdx,%rcx
	lea -1(%rdi,%rdx),%rdi
	lea -1(%rsi,%rdx),%rsi
	std
	rep movsb
	cld
	lea 1(%rdi),%rax
	ret
END(memmove)

ALIAS(memmove, __unsanitized_memmove)
ASAN_WEAK_ALIAS(memmove)
