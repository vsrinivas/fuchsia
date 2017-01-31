.global fmodl
.type fmodl,@function
fmodl:
	.cfi_startproc
	fldt 24(%rsp)
	fldt 8(%rsp)
1:	fprem
	fnstsw %ax
	testb $4,%ah
	jnz 1b
	fstp %st(1)
	ret
	.cfi_endproc
