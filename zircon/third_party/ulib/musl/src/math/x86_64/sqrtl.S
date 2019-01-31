.global sqrtl
.type sqrtl,@function
sqrtl:
	.cfi_startproc
	fldt 8(%rsp)
	fsqrt
	ret
	.cfi_endproc
