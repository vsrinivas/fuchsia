.global sqrt
.type sqrt,@function
sqrt:
	.cfi_startproc
	sqrtsd %xmm0, %xmm0
	ret
	.cfi_endproc
