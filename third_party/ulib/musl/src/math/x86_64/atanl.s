.global atanl
.type atanl,@function
atanl:
	.cfi_startproc
	fldt 8(%rsp)
	fld1
	fpatan
	ret
	.cfi_endproc
