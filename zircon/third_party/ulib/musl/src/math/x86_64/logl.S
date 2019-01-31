.global logl
.type logl,@function
logl:
	.cfi_startproc
	fldln2
	fldt 8(%rsp)
	fyl2x
	ret
	.cfi_endproc
