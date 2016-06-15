# use acos(x) = atan2(fabs(sqrt((1-x)*(1+x))), x)

.global acosl
.type acosl,@function
acosl:
	fldt 8(%rsp)
1:	fld %st(0)
	fld1
	fsub %st(0),%st(1)
	fadd %st(2)
	fmulp
	fsqrt
	fabs
	fxch %st(1)
	fpatan
	ret
