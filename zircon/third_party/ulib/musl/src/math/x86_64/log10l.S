.global log10l
.type log10l,@function
log10l:
	.cfi_startproc
	fldlg2
	fldt 8(%rsp)
	fyl2x
	ret
	.cfi_endproc
