.global fabsl
.type fabsl,@function
fabsl:
	.cfi_startproc
	fldt 8(%rsp)
	fabs
	ret
	.cfi_endproc
