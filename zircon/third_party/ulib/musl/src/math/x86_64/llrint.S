.global llrint
.type llrint,@function
llrint:
	.cfi_startproc
	cvtsd2si %xmm0,%rax
	ret
	.cfi_endproc
