.global floorl
.type floorl,@function
floorl:
	.cfi_startproc
	fldt 8(%rsp)
1:	mov $0x7,%al
1:	fstcw 8(%rsp)
	mov 9(%rsp),%ah
	mov %al,9(%rsp)
	fldcw 8(%rsp)
	frndint
	mov %ah,9(%rsp)
	fldcw 8(%rsp)
	ret
	.cfi_endproc

.global ceill
.type ceill,@function
ceill:
	.cfi_startproc
	fldt 8(%rsp)
	mov $0xb,%al
	jmp 1b
	.cfi_endproc

.global truncl
.type truncl,@function
truncl:
	.cfi_startproc
	fldt 8(%rsp)
	mov $0xf,%al
	jmp 1b
	.cfi_endproc
