.global lrint
.type lrint,@function
lrint:
	.cfi_startproc
	cvtsd2si %xmm0,%rax
	ret
	.cfi_endproc
