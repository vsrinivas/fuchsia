.global llrintl
.type llrintl,@function
llrintl:
	.cfi_startproc
	fldt 8(%rsp)
	fistpll 8(%rsp)
	mov 8(%rsp),%rax
	ret
	.cfi_endproc
