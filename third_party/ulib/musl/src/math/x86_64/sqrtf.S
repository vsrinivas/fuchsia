.global sqrtf
.type sqrtf,@function
sqrtf:
	.cfi_startproc
        sqrtss %xmm0, %xmm0
	ret
	.cfi_endproc
