.global llrintf
.type llrintf,@function
llrintf:
	.cfi_startproc
	cvtss2si %xmm0,%rax
	ret
	.cfi_endproc
