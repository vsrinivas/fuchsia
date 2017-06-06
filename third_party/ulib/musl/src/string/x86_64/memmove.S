#include "asm.h"

ENTRY(memmove)
ALIAS_ENTRY(__unsanitized_memmove)
ASAN_ALIAS_ENTRY(memmove)

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

ASAN_ALIAS_END(memmove)
ALIAS_END(__unsanitized_memmove)
END(memmove)
