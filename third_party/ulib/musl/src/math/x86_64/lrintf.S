.global lrintf
.type lrintf,@function
lrintf:
	.cfi_startproc
	cvtss2si %xmm0,%rax
	ret
	.cfi_endproc
