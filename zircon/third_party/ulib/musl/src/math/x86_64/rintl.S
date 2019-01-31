.global rintl
.type rintl,@function
rintl:
	.cfi_startproc
	fldt 8(%rsp)
	frndint
	ret
	.cfi_endproc
