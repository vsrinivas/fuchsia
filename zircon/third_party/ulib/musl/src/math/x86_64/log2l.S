.global log2l
.type log2l,@function
log2l:
	.cfi_startproc
	fld1
	fldt 8(%rsp)
	fyl2x
	ret
	.cfi_endproc
