; This file is generated from a similarly-named Perl script in the BoringSSL
; source tree. Do not edit by hand.

%ifdef BORINGSSL_PREFIX
%include "boringssl_prefix_symbols_nasm.inc"
%endif
%ifidn __OUTPUT_FORMAT__,obj
section	code	use32 class=code align=64
%elifidn __OUTPUT_FORMAT__,win32
%ifdef __YASM_VERSION_ID__
%if __YASM_VERSION_ID__ < 01010000h
%error yasm version 1.1.0 or later needed.
%endif
; Yasm automatically includes .00 and complains about redefining it.
; https://www.tortall.net/projects/yasm/manual/html/objfmt-win32-safeseh.html
%else
$@feat.00 equ 1
%endif
section	.text	code align=64
%else
section	.text	code
%endif
align	16
__x86_AES_encrypt_compact:
	mov	DWORD [20+esp],edi
	xor	eax,DWORD [edi]
	xor	ebx,DWORD [4+edi]
	xor	ecx,DWORD [8+edi]
	xor	edx,DWORD [12+edi]
	mov	esi,DWORD [240+edi]
	lea	esi,[esi*1+esi-2]
	lea	esi,[esi*8+edi]
	mov	DWORD [24+esp],esi
	mov	edi,DWORD [ebp-128]
	mov	esi,DWORD [ebp-96]
	mov	edi,DWORD [ebp-64]
	mov	esi,DWORD [ebp-32]
	mov	edi,DWORD [ebp]
	mov	esi,DWORD [32+ebp]
	mov	edi,DWORD [64+ebp]
	mov	esi,DWORD [96+ebp]
align	16
L$000loop:
	mov	esi,eax
	and	esi,255
	movzx	esi,BYTE [esi*1+ebp-128]
	movzx	edi,bh
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,8
	xor	esi,edi
	mov	edi,ecx
	shr	edi,16
	and	edi,255
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,16
	xor	esi,edi
	mov	edi,edx
	shr	edi,24
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,24
	xor	esi,edi
	mov	DWORD [4+esp],esi
	mov	esi,ebx
	and	esi,255
	shr	ebx,16
	movzx	esi,BYTE [esi*1+ebp-128]
	movzx	edi,ch
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,8
	xor	esi,edi
	mov	edi,edx
	shr	edi,16
	and	edi,255
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,16
	xor	esi,edi
	mov	edi,eax
	shr	edi,24
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,24
	xor	esi,edi
	mov	DWORD [8+esp],esi
	mov	esi,ecx
	and	esi,255
	shr	ecx,24
	movzx	esi,BYTE [esi*1+ebp-128]
	movzx	edi,dh
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,8
	xor	esi,edi
	mov	edi,eax
	shr	edi,16
	and	edx,255
	and	edi,255
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,16
	xor	esi,edi
	movzx	edi,bh
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,24
	xor	esi,edi
	and	edx,255
	movzx	edx,BYTE [edx*1+ebp-128]
	movzx	eax,ah
	movzx	eax,BYTE [eax*1+ebp-128]
	shl	eax,8
	xor	edx,eax
	mov	eax,DWORD [4+esp]
	and	ebx,255
	movzx	ebx,BYTE [ebx*1+ebp-128]
	shl	ebx,16
	xor	edx,ebx
	mov	ebx,DWORD [8+esp]
	movzx	ecx,BYTE [ecx*1+ebp-128]
	shl	ecx,24
	xor	edx,ecx
	mov	ecx,esi
	mov	ebp,2155905152
	and	ebp,ecx
	lea	edi,[ecx*1+ecx]
	mov	esi,ebp
	shr	ebp,7
	and	edi,4278124286
	sub	esi,ebp
	mov	ebp,ecx
	and	esi,454761243
	ror	ebp,16
	xor	esi,edi
	mov	edi,ecx
	xor	ecx,esi
	ror	edi,24
	xor	esi,ebp
	rol	ecx,24
	xor	esi,edi
	mov	ebp,2155905152
	xor	ecx,esi
	and	ebp,edx
	lea	edi,[edx*1+edx]
	mov	esi,ebp
	shr	ebp,7
	and	edi,4278124286
	sub	esi,ebp
	mov	ebp,edx
	and	esi,454761243
	ror	ebp,16
	xor	esi,edi
	mov	edi,edx
	xor	edx,esi
	ror	edi,24
	xor	esi,ebp
	rol	edx,24
	xor	esi,edi
	mov	ebp,2155905152
	xor	edx,esi
	and	ebp,eax
	lea	edi,[eax*1+eax]
	mov	esi,ebp
	shr	ebp,7
	and	edi,4278124286
	sub	esi,ebp
	mov	ebp,eax
	and	esi,454761243
	ror	ebp,16
	xor	esi,edi
	mov	edi,eax
	xor	eax,esi
	ror	edi,24
	xor	esi,ebp
	rol	eax,24
	xor	esi,edi
	mov	ebp,2155905152
	xor	eax,esi
	and	ebp,ebx
	lea	edi,[ebx*1+ebx]
	mov	esi,ebp
	shr	ebp,7
	and	edi,4278124286
	sub	esi,ebp
	mov	ebp,ebx
	and	esi,454761243
	ror	ebp,16
	xor	esi,edi
	mov	edi,ebx
	xor	ebx,esi
	ror	edi,24
	xor	esi,ebp
	rol	ebx,24
	xor	esi,edi
	xor	ebx,esi
	mov	edi,DWORD [20+esp]
	mov	ebp,DWORD [28+esp]
	add	edi,16
	xor	eax,DWORD [edi]
	xor	ebx,DWORD [4+edi]
	xor	ecx,DWORD [8+edi]
	xor	edx,DWORD [12+edi]
	cmp	edi,DWORD [24+esp]
	mov	DWORD [20+esp],edi
	jb	NEAR L$000loop
	mov	esi,eax
	and	esi,255
	movzx	esi,BYTE [esi*1+ebp-128]
	movzx	edi,bh
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,8
	xor	esi,edi
	mov	edi,ecx
	shr	edi,16
	and	edi,255
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,16
	xor	esi,edi
	mov	edi,edx
	shr	edi,24
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,24
	xor	esi,edi
	mov	DWORD [4+esp],esi
	mov	esi,ebx
	and	esi,255
	shr	ebx,16
	movzx	esi,BYTE [esi*1+ebp-128]
	movzx	edi,ch
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,8
	xor	esi,edi
	mov	edi,edx
	shr	edi,16
	and	edi,255
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,16
	xor	esi,edi
	mov	edi,eax
	shr	edi,24
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,24
	xor	esi,edi
	mov	DWORD [8+esp],esi
	mov	esi,ecx
	and	esi,255
	shr	ecx,24
	movzx	esi,BYTE [esi*1+ebp-128]
	movzx	edi,dh
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,8
	xor	esi,edi
	mov	edi,eax
	shr	edi,16
	and	edx,255
	and	edi,255
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,16
	xor	esi,edi
	movzx	edi,bh
	movzx	edi,BYTE [edi*1+ebp-128]
	shl	edi,24
	xor	esi,edi
	mov	edi,DWORD [20+esp]
	and	edx,255
	movzx	edx,BYTE [edx*1+ebp-128]
	movzx	eax,ah
	movzx	eax,BYTE [eax*1+ebp-128]
	shl	eax,8
	xor	edx,eax
	mov	eax,DWORD [4+esp]
	and	ebx,255
	movzx	ebx,BYTE [ebx*1+ebp-128]
	shl	ebx,16
	xor	edx,ebx
	mov	ebx,DWORD [8+esp]
	movzx	ecx,BYTE [ecx*1+ebp-128]
	shl	ecx,24
	xor	edx,ecx
	mov	ecx,esi
	xor	eax,DWORD [16+edi]
	xor	ebx,DWORD [20+edi]
	xor	ecx,DWORD [24+edi]
	xor	edx,DWORD [28+edi]
	ret
align	16
__sse_AES_encrypt_compact:
	pxor	mm0,[edi]
	pxor	mm4,[8+edi]
	mov	esi,DWORD [240+edi]
	lea	esi,[esi*1+esi-2]
	lea	esi,[esi*8+edi]
	mov	DWORD [24+esp],esi
	mov	eax,454761243
	mov	DWORD [8+esp],eax
	mov	DWORD [12+esp],eax
	mov	eax,DWORD [ebp-128]
	mov	ebx,DWORD [ebp-96]
	mov	ecx,DWORD [ebp-64]
	mov	edx,DWORD [ebp-32]
	mov	eax,DWORD [ebp]
	mov	ebx,DWORD [32+ebp]
	mov	ecx,DWORD [64+ebp]
	mov	edx,DWORD [96+ebp]
align	16
L$001loop:
	pshufw	mm1,mm0,8
	pshufw	mm5,mm4,13
	movd	eax,mm1
	movd	ebx,mm5
	mov	DWORD [20+esp],edi
	movzx	esi,al
	movzx	edx,ah
	pshufw	mm2,mm0,13
	movzx	ecx,BYTE [esi*1+ebp-128]
	movzx	edi,bl
	movzx	edx,BYTE [edx*1+ebp-128]
	shr	eax,16
	shl	edx,8
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,bh
	shl	esi,16
	pshufw	mm6,mm4,8
	or	ecx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,ah
	shl	esi,24
	shr	ebx,16
	or	edx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,bh
	shl	esi,8
	or	ecx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,al
	shl	esi,24
	or	ecx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,bl
	movd	eax,mm2
	movd	mm0,ecx
	movzx	ecx,BYTE [edi*1+ebp-128]
	movzx	edi,ah
	shl	ecx,16
	movd	ebx,mm6
	or	ecx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,bh
	shl	esi,24
	or	ecx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,bl
	shl	esi,8
	shr	ebx,16
	or	ecx,esi
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,al
	shr	eax,16
	movd	mm1,ecx
	movzx	ecx,BYTE [edi*1+ebp-128]
	movzx	edi,ah
	shl	ecx,16
	and	eax,255
	or	ecx,esi
	punpckldq	mm0,mm1
	movzx	esi,BYTE [edi*1+ebp-128]
	movzx	edi,bh
	shl	esi,24
	and	ebx,255
	movzx	eax,BYTE [eax*1+ebp-128]
	or	ecx,esi
	shl	eax,16
	movzx	esi,BYTE [edi*1+ebp-128]
	or	edx,eax
	shl	esi,8
	movzx	ebx,BYTE [ebx*1+ebp-128]
	or	ecx,esi
	or	edx,ebx
	mov	edi,DWORD [20+esp]
	movd	mm4,ecx
	movd	mm5,edx
	punpckldq	mm4,mm5
	add	edi,16
	cmp	edi,DWORD [24+esp]
	ja	NEAR L$002out
	movq	mm2,[8+esp]
	pxor	mm3,mm3
	pxor	mm7,mm7
	movq	mm1,mm0
	movq	mm5,mm4
	pcmpgtb	mm3,mm0
	pcmpgtb	mm7,mm4
	pand	mm3,mm2
	pand	mm7,mm2
	pshufw	mm2,mm0,177
	pshufw	mm6,mm4,177
	paddb	mm0,mm0
	paddb	mm4,mm4
	pxor	mm0,mm3
	pxor	mm4,mm7
	pshufw	mm3,mm2,177
	pshufw	mm7,mm6,177
	pxor	mm1,mm0
	pxor	mm5,mm4
	pxor	mm0,mm2
	pxor	mm4,mm6
	movq	mm2,mm3
	movq	mm6,mm7
	pslld	mm3,8
	pslld	mm7,8
	psrld	mm2,24
	psrld	mm6,24
	pxor	mm0,mm3
	pxor	mm4,mm7
	pxor	mm0,mm2
	pxor	mm4,mm6
	movq	mm3,mm1
	movq	mm7,mm5
	movq	mm2,[edi]
	movq	mm6,[8+edi]
	psrld	mm1,8
	psrld	mm5,8
	mov	eax,DWORD [ebp-128]
	pslld	mm3,24
	pslld	mm7,24
	mov	ebx,DWORD [ebp-64]
	pxor	mm0,mm1
	pxor	mm4,mm5
	mov	ecx,DWORD [ebp]
	pxor	mm0,mm3
	pxor	mm4,mm7
	mov	edx,DWORD [64+ebp]
	pxor	mm0,mm2
	pxor	mm4,mm6
	jmp	NEAR L$001loop
align	16
L$002out:
	pxor	mm0,[edi]
	pxor	mm4,[8+edi]
	ret
align	16
__x86_AES_encrypt:
	mov	DWORD [20+esp],edi
	xor	eax,DWORD [edi]
	xor	ebx,DWORD [4+edi]
	xor	ecx,DWORD [8+edi]
	xor	edx,DWORD [12+edi]
	mov	esi,DWORD [240+edi]
	lea	esi,[esi*1+esi-2]
	lea	esi,[esi*8+edi]
	mov	DWORD [24+esp],esi
align	16
L$003loop:
	mov	esi,eax
	and	esi,255
	mov	esi,DWORD [esi*8+ebp]
	movzx	edi,bh
	xor	esi,DWORD [3+edi*8+ebp]
	mov	edi,ecx
	shr	edi,16
	and	edi,255
	xor	esi,DWORD [2+edi*8+ebp]
	mov	edi,edx
	shr	edi,24
	xor	esi,DWORD [1+edi*8+ebp]
	mov	DWORD [4+esp],esi
	mov	esi,ebx
	and	esi,255
	shr	ebx,16
	mov	esi,DWORD [esi*8+ebp]
	movzx	edi,ch
	xor	esi,DWORD [3+edi*8+ebp]
	mov	edi,edx
	shr	edi,16
	and	edi,255
	xor	esi,DWORD [2+edi*8+ebp]
	mov	edi,eax
	shr	edi,24
	xor	esi,DWORD [1+edi*8+ebp]
	mov	DWORD [8+esp],esi
	mov	esi,ecx
	and	esi,255
	shr	ecx,24
	mov	esi,DWORD [esi*8+ebp]
	movzx	edi,dh
	xor	esi,DWORD [3+edi*8+ebp]
	mov	edi,eax
	shr	edi,16
	and	edx,255
	and	edi,255
	xor	esi,DWORD [2+edi*8+ebp]
	movzx	edi,bh
	xor	esi,DWORD [1+edi*8+ebp]
	mov	edi,DWORD [20+esp]
	mov	edx,DWORD [edx*8+ebp]
	movzx	eax,ah
	xor	edx,DWORD [3+eax*8+ebp]
	mov	eax,DWORD [4+esp]
	and	ebx,255
	xor	edx,DWORD [2+ebx*8+ebp]
	mov	ebx,DWORD [8+esp]
	xor	edx,DWORD [1+ecx*8+ebp]
	mov	ecx,esi
	add	edi,16
	xor	eax,DWORD [edi]
	xor	ebx,DWORD [4+edi]
	xor	ecx,DWORD [8+edi]
	xor	edx,DWORD [12+edi]
	cmp	edi,DWORD [24+esp]
	mov	DWORD [20+esp],edi
	jb	NEAR L$003loop
	mov	esi,eax
	and	esi,255
	mov	esi,DWORD [2+esi*8+ebp]
	and	esi,255
	movzx	edi,bh
	mov	edi,DWORD [edi*8+ebp]
	and	edi,65280
	xor	esi,edi
	mov	edi,ecx
	shr	edi,16
	and	edi,255
	mov	edi,DWORD [edi*8+ebp]
	and	edi,16711680
	xor	esi,edi
	mov	edi,edx
	shr	edi,24
	mov	edi,DWORD [2+edi*8+ebp]
	and	edi,4278190080
	xor	esi,edi
	mov	DWORD [4+esp],esi
	mov	esi,ebx
	and	esi,255
	shr	ebx,16
	mov	esi,DWORD [2+esi*8+ebp]
	and	esi,255
	movzx	edi,ch
	mov	edi,DWORD [edi*8+ebp]
	and	edi,65280
	xor	esi,edi
	mov	edi,edx
	shr	edi,16
	and	edi,255
	mov	edi,DWORD [edi*8+ebp]
	and	edi,16711680
	xor	esi,edi
	mov	edi,eax
	shr	edi,24
	mov	edi,DWORD [2+edi*8+ebp]
	and	edi,4278190080
	xor	esi,edi
	mov	DWORD [8+esp],esi
	mov	esi,ecx
	and	esi,255
	shr	ecx,24
	mov	esi,DWORD [2+esi*8+ebp]
	and	esi,255
	movzx	edi,dh
	mov	edi,DWORD [edi*8+ebp]
	and	edi,65280
	xor	esi,edi
	mov	edi,eax
	shr	edi,16
	and	edx,255
	and	edi,255
	mov	edi,DWORD [edi*8+ebp]
	and	edi,16711680
	xor	esi,edi
	movzx	edi,bh
	mov	edi,DWORD [2+edi*8+ebp]
	and	edi,4278190080
	xor	esi,edi
	mov	edi,DWORD [20+esp]
	and	edx,255
	mov	edx,DWORD [2+edx*8+ebp]
	and	edx,255
	movzx	eax,ah
	mov	eax,DWORD [eax*8+ebp]
	and	eax,65280
	xor	edx,eax
	mov	eax,DWORD [4+esp]
	and	ebx,255
	mov	ebx,DWORD [ebx*8+ebp]
	and	ebx,16711680
	xor	edx,ebx
	mov	ebx,DWORD [8+esp]
	mov	ecx,DWORD [2+ecx*8+ebp]
	and	ecx,4278190080
	xor	edx,ecx
	mov	ecx,esi
	add	edi,16
	xor	eax,DWORD [edi]
	xor	ebx,DWORD [4+edi]
	xor	ecx,DWORD [8+edi]
	xor	edx,DWORD [12+edi]
	ret
align	64
L$AES_Te:
dd	2774754246,2774754246
dd	2222750968,2222750968
dd	2574743534,2574743534
dd	2373680118,2373680118
dd	234025727,234025727
dd	3177933782,3177933782
dd	2976870366,2976870366
dd	1422247313,1422247313
dd	1345335392,1345335392
dd	50397442,50397442
dd	2842126286,2842126286
dd	2099981142,2099981142
dd	436141799,436141799
dd	1658312629,1658312629
dd	3870010189,3870010189
dd	2591454956,2591454956
dd	1170918031,1170918031
dd	2642575903,2642575903
dd	1086966153,1086966153
dd	2273148410,2273148410
dd	368769775,368769775
dd	3948501426,3948501426
dd	3376891790,3376891790
dd	200339707,200339707
dd	3970805057,3970805057
dd	1742001331,1742001331
dd	4255294047,4255294047
dd	3937382213,3937382213
dd	3214711843,3214711843
dd	4154762323,4154762323
dd	2524082916,2524082916
dd	1539358875,1539358875
dd	3266819957,3266819957
dd	486407649,486407649
dd	2928907069,2928907069
dd	1780885068,1780885068
dd	1513502316,1513502316
dd	1094664062,1094664062
dd	49805301,49805301
dd	1338821763,1338821763
dd	1546925160,1546925160
dd	4104496465,4104496465
dd	887481809,887481809
dd	150073849,150073849
dd	2473685474,2473685474
dd	1943591083,1943591083
dd	1395732834,1395732834
dd	1058346282,1058346282
dd	201589768,201589768
dd	1388824469,1388824469
dd	1696801606,1696801606
dd	1589887901,1589887901
dd	672667696,672667696
dd	2711000631,2711000631
dd	251987210,251987210
dd	3046808111,3046808111
dd	151455502,151455502
dd	907153956,907153956
dd	2608889883,2608889883
dd	1038279391,1038279391
dd	652995533,652995533
dd	1764173646,1764173646
dd	3451040383,3451040383
dd	2675275242,2675275242
dd	453576978,453576978
dd	2659418909,2659418909
dd	1949051992,1949051992
dd	773462580,773462580
dd	756751158,756751158
dd	2993581788,2993581788
dd	3998898868,3998898868
dd	4221608027,4221608027
dd	4132590244,4132590244
dd	1295727478,1295727478
dd	1641469623,1641469623
dd	3467883389,3467883389
dd	2066295122,2066295122
dd	1055122397,1055122397
dd	1898917726,1898917726
dd	2542044179,2542044179
dd	4115878822,4115878822
dd	1758581177,1758581177
dd	0,0
dd	753790401,753790401
dd	1612718144,1612718144
dd	536673507,536673507
dd	3367088505,3367088505
dd	3982187446,3982187446
dd	3194645204,3194645204
dd	1187761037,1187761037
dd	3653156455,3653156455
dd	1262041458,1262041458
dd	3729410708,3729410708
dd	3561770136,3561770136
dd	3898103984,3898103984
dd	1255133061,1255133061
dd	1808847035,1808847035
dd	720367557,720367557
dd	3853167183,3853167183
dd	385612781,385612781
dd	3309519750,3309519750
dd	3612167578,3612167578
dd	1429418854,1429418854
dd	2491778321,2491778321
dd	3477423498,3477423498
dd	284817897,284817897
dd	100794884,100794884
dd	2172616702,2172616702
dd	4031795360,4031795360
dd	1144798328,1144798328
dd	3131023141,3131023141
dd	3819481163,3819481163
dd	4082192802,4082192802
dd	4272137053,4272137053
dd	3225436288,3225436288
dd	2324664069,2324664069
dd	2912064063,2912064063
dd	3164445985,3164445985
dd	1211644016,1211644016
dd	83228145,83228145
dd	3753688163,3753688163
dd	3249976951,3249976951
dd	1977277103,1977277103
dd	1663115586,1663115586
dd	806359072,806359072
dd	452984805,452984805
dd	250868733,250868733
dd	1842533055,1842533055
dd	1288555905,1288555905
dd	336333848,336333848
dd	890442534,890442534
dd	804056259,804056259
dd	3781124030,3781124030
dd	2727843637,2727843637
dd	3427026056,3427026056
dd	957814574,957814574
dd	1472513171,1472513171
dd	4071073621,4071073621
dd	2189328124,2189328124
dd	1195195770,1195195770
dd	2892260552,2892260552
dd	3881655738,3881655738
dd	723065138,723065138
dd	2507371494,2507371494
dd	2690670784,2690670784
dd	2558624025,2558624025
dd	3511635870,3511635870
dd	2145180835,2145180835
dd	1713513028,1713513028
dd	2116692564,2116692564
dd	2878378043,2878378043
dd	2206763019,2206763019
dd	3393603212,3393603212
dd	703524551,703524551
dd	3552098411,3552098411
dd	1007948840,1007948840
dd	2044649127,2044649127
dd	3797835452,3797835452
dd	487262998,487262998
dd	1994120109,1994120109
dd	1004593371,1004593371
dd	1446130276,1446130276
dd	1312438900,1312438900
dd	503974420,503974420
dd	3679013266,3679013266
dd	168166924,168166924
dd	1814307912,1814307912
dd	3831258296,3831258296
dd	1573044895,1573044895
dd	1859376061,1859376061
dd	4021070915,4021070915
dd	2791465668,2791465668
dd	2828112185,2828112185
dd	2761266481,2761266481
dd	937747667,937747667
dd	2339994098,2339994098
dd	854058965,854058965
dd	1137232011,1137232011
dd	1496790894,1496790894
dd	3077402074,3077402074
dd	2358086913,2358086913
dd	1691735473,1691735473
dd	3528347292,3528347292
dd	3769215305,3769215305
dd	3027004632,3027004632
dd	4199962284,4199962284
dd	133494003,133494003
dd	636152527,636152527
dd	2942657994,2942657994
dd	2390391540,2390391540
dd	3920539207,3920539207
dd	403179536,403179536
dd	3585784431,3585784431
dd	2289596656,2289596656
dd	1864705354,1864705354
dd	1915629148,1915629148
dd	605822008,605822008
dd	4054230615,4054230615
dd	3350508659,3350508659
dd	1371981463,1371981463
dd	602466507,602466507
dd	2094914977,2094914977
dd	2624877800,2624877800
dd	555687742,555687742
dd	3712699286,3712699286
dd	3703422305,3703422305
dd	2257292045,2257292045
dd	2240449039,2240449039
dd	2423288032,2423288032
dd	1111375484,1111375484
dd	3300242801,3300242801
dd	2858837708,2858837708
dd	3628615824,3628615824
dd	84083462,84083462
dd	32962295,32962295
dd	302911004,302911004
dd	2741068226,2741068226
dd	1597322602,1597322602
dd	4183250862,4183250862
dd	3501832553,3501832553
dd	2441512471,2441512471
dd	1489093017,1489093017
dd	656219450,656219450
dd	3114180135,3114180135
dd	954327513,954327513
dd	335083755,335083755
dd	3013122091,3013122091
dd	856756514,856756514
dd	3144247762,3144247762
dd	1893325225,1893325225
dd	2307821063,2307821063
dd	2811532339,2811532339
dd	3063651117,3063651117
dd	572399164,572399164
dd	2458355477,2458355477
dd	552200649,552200649
dd	1238290055,1238290055
dd	4283782570,4283782570
dd	2015897680,2015897680
dd	2061492133,2061492133
dd	2408352771,2408352771
dd	4171342169,4171342169
dd	2156497161,2156497161
dd	386731290,386731290
dd	3669999461,3669999461
dd	837215959,837215959
dd	3326231172,3326231172
dd	3093850320,3093850320
dd	3275833730,3275833730
dd	2962856233,2962856233
dd	1999449434,1999449434
dd	286199582,286199582
dd	3417354363,3417354363
dd	4233385128,4233385128
dd	3602627437,3602627437
dd	974525996,974525996
db	99,124,119,123,242,107,111,197
db	48,1,103,43,254,215,171,118
db	202,130,201,125,250,89,71,240
db	173,212,162,175,156,164,114,192
db	183,253,147,38,54,63,247,204
db	52,165,229,241,113,216,49,21
db	4,199,35,195,24,150,5,154
db	7,18,128,226,235,39,178,117
db	9,131,44,26,27,110,90,160
db	82,59,214,179,41,227,47,132
db	83,209,0,237,32,252,177,91
db	106,203,190,57,74,76,88,207
db	208,239,170,251,67,77,51,133
db	69,249,2,127,80,60,159,168
db	81,163,64,143,146,157,56,245
db	188,182,218,33,16,255,243,210
db	205,12,19,236,95,151,68,23
db	196,167,126,61,100,93,25,115
db	96,129,79,220,34,42,144,136
db	70,238,184,20,222,94,11,219
db	224,50,58,10,73,6,36,92
db	194,211,172,98,145,149,228,121
db	231,200,55,109,141,213,78,169
db	108,86,244,234,101,122,174,8
db	186,120,37,46,28,166,180,198
db	232,221,116,31,75,189,139,138
db	112,62,181,102,72,3,246,14
db	97,53,87,185,134,193,29,158
db	225,248,152,17,105,217,142,148
db	155,30,135,233,206,85,40,223
db	140,161,137,13,191,230,66,104
db	65,153,45,15,176,84,187,22
db	99,124,119,123,242,107,111,197
db	48,1,103,43,254,215,171,118
db	202,130,201,125,250,89,71,240
db	173,212,162,175,156,164,114,192
db	183,253,147,38,54,63,247,204
db	52,165,229,241,113,216,49,21
db	4,199,35,195,24,150,5,154
db	7,18,128,226,235,39,178,117
db	9,131,44,26,27,110,90,160
db	82,59,214,179,41,227,47,132
db	83,209,0,237,32,252,177,91
db	106,203,190,57,74,76,88,207
db	208,239,170,251,67,77,51,133
db	69,249,2,127,80,60,159,168
db	81,163,64,143,146,157,56,245
db	188,182,218,33,16,255,243,210
db	205,12,19,236,95,151,68,23
db	196,167,126,61,100,93,25,115
db	96,129,79,220,34,42,144,136
db	70,238,184,20,222,94,11,219
db	224,50,58,10,73,6,36,92
db	194,211,172,98,145,149,228,121
db	231,200,55,109,141,213,78,169
db	108,86,244,234,101,122,174,8
db	186,120,37,46,28,166,180,198
db	232,221,116,31,75,189,139,138
db	112,62,181,102,72,3,246,14
db	97,53,87,185,134,193,29,158
db	225,248,152,17,105,217,142,148
db	155,30,135,233,206,85,40,223
db	140,161,137,13,191,230,66,104
db	65,153,45,15,176,84,187,22
db	99,124,119,123,242,107,111,197
db	48,1,103,43,254,215,171,118
db	202,130,201,125,250,89,71,240
db	173,212,162,175,156,164,114,192
db	183,253,147,38,54,63,247,204
db	52,165,229,241,113,216,49,21
db	4,199,35,195,24,150,5,154
db	7,18,128,226,235,39,178,117
db	9,131,44,26,27,110,90,160
db	82,59,214,179,41,227,47,132
db	83,209,0,237,32,252,177,91
db	106,203,190,57,74,76,88,207
db	208,239,170,251,67,77,51,133
db	69,249,2,127,80,60,159,168
db	81,163,64,143,146,157,56,245
db	188,182,218,33,16,255,243,210
db	205,12,19,236,95,151,68,23
db	196,167,126,61,100,93,25,115
db	96,129,79,220,34,42,144,136
db	70,238,184,20,222,94,11,219
db	224,50,58,10,73,6,36,92
db	194,211,172,98,145,149,228,121
db	231,200,55,109,141,213,78,169
db	108,86,244,234,101,122,174,8
db	186,120,37,46,28,166,180,198
db	232,221,116,31,75,189,139,138
db	112,62,181,102,72,3,246,14
db	97,53,87,185,134,193,29,158
db	225,248,152,17,105,217,142,148
db	155,30,135,233,206,85,40,223
db	140,161,137,13,191,230,66,104
db	65,153,45,15,176,84,187,22
db	99,124,119,123,242,107,111,197
db	48,1,103,43,254,215,171,118
db	202,130,201,125,250,89,71,240
db	173,212,162,175,156,164,114,192
db	183,253,147,38,54,63,247,204
db	52,165,229,241,113,216,49,21
db	4,199,35,195,24,150,5,154
db	7,18,128,226,235,39,178,117
db	9,131,44,26,27,110,90,160
db	82,59,214,179,41,227,47,132
db	83,209,0,237,32,252,177,91
db	106,203,190,57,74,76,88,207
db	208,239,170,251,67,77,51,133
db	69,249,2,127,80,60,159,168
db	81,163,64,143,146,157,56,245
db	188,182,218,33,16,255,243,210
db	205,12,19,236,95,151,68,23
db	196,167,126,61,100,93,25,115
db	96,129,79,220,34,42,144,136
db	70,238,184,20,222,94,11,219
db	224,50,58,10,73,6,36,92
db	194,211,172,98,145,149,228,121
db	231,200,55,109,141,213,78,169
db	108,86,244,234,101,122,174,8
db	186,120,37,46,28,166,180,198
db	232,221,116,31,75,189,139,138
db	112,62,181,102,72,3,246,14
db	97,53,87,185,134,193,29,158
db	225,248,152,17,105,217,142,148
db	155,30,135,233,206,85,40,223
db	140,161,137,13,191,230,66,104
db	65,153,45,15,176,84,187,22
dd	1,2,4,8
dd	16,32,64,128
dd	27,54,0,0
dd	0,0,0,0
global	_GFp_aes_nohw_encrypt
align	16
_GFp_aes_nohw_encrypt:
L$_GFp_aes_nohw_encrypt_begin:
	push	ebp
	push	ebx
	push	esi
	push	edi
	mov	esi,DWORD [20+esp]
	mov	edi,DWORD [28+esp]
	mov	eax,esp
	sub	esp,36
	and	esp,-64
	lea	ebx,[edi-127]
	sub	ebx,esp
	neg	ebx
	and	ebx,960
	sub	esp,ebx
	add	esp,4
	mov	DWORD [28+esp],eax
	call	L$004pic_point
L$004pic_point:
	pop	ebp
	lea	eax,[_GFp_ia32cap_P]
	lea	ebp,[(L$AES_Te-L$004pic_point)+ebp]
	lea	ebx,[764+esp]
	sub	ebx,ebp
	and	ebx,768
	lea	ebp,[2176+ebx*1+ebp]
	bt	DWORD [eax],25
	jnc	NEAR L$005x86
	movq	mm0,[esi]
	movq	mm4,[8+esi]
	call	__sse_AES_encrypt_compact
	mov	esp,DWORD [28+esp]
	mov	esi,DWORD [24+esp]
	movq	[esi],mm0
	movq	[8+esi],mm4
	emms
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
align	16
L$005x86:
	mov	DWORD [24+esp],ebp
	mov	eax,DWORD [esi]
	mov	ebx,DWORD [4+esi]
	mov	ecx,DWORD [8+esi]
	mov	edx,DWORD [12+esi]
	call	__x86_AES_encrypt_compact
	mov	esp,DWORD [28+esp]
	mov	esi,DWORD [24+esp]
	mov	DWORD [esi],eax
	mov	DWORD [4+esi],ebx
	mov	DWORD [8+esi],ecx
	mov	DWORD [12+esi],edx
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
align	16
__x86_AES_set_encrypt_key:
	push	ebp
	push	ebx
	push	esi
	push	edi
	mov	esi,DWORD [24+esp]
	mov	edi,DWORD [32+esp]
	test	esi,-1
	jz	NEAR L$006badpointer
	test	edi,-1
	jz	NEAR L$006badpointer
	call	L$007pic_point
L$007pic_point:
	pop	ebp
	lea	ebp,[(L$AES_Te-L$007pic_point)+ebp]
	lea	ebp,[2176+ebp]
	mov	eax,DWORD [ebp-128]
	mov	ebx,DWORD [ebp-96]
	mov	ecx,DWORD [ebp-64]
	mov	edx,DWORD [ebp-32]
	mov	eax,DWORD [ebp]
	mov	ebx,DWORD [32+ebp]
	mov	ecx,DWORD [64+ebp]
	mov	edx,DWORD [96+ebp]
	mov	ecx,DWORD [28+esp]
	cmp	ecx,128
	je	NEAR L$00810rounds
	cmp	ecx,256
	je	NEAR L$00914rounds
	mov	eax,-2
	jmp	NEAR L$010exit
L$00810rounds:
	mov	eax,DWORD [esi]
	mov	ebx,DWORD [4+esi]
	mov	ecx,DWORD [8+esi]
	mov	edx,DWORD [12+esi]
	mov	DWORD [edi],eax
	mov	DWORD [4+edi],ebx
	mov	DWORD [8+edi],ecx
	mov	DWORD [12+edi],edx
	xor	ecx,ecx
	jmp	NEAR L$01110shortcut
align	4
L$01210loop:
	mov	eax,DWORD [edi]
	mov	edx,DWORD [12+edi]
L$01110shortcut:
	movzx	esi,dl
	movzx	ebx,BYTE [esi*1+ebp-128]
	movzx	esi,dh
	shl	ebx,24
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	shr	edx,16
	movzx	esi,dl
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	movzx	esi,dh
	shl	ebx,8
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	shl	ebx,16
	xor	eax,ebx
	xor	eax,DWORD [896+ecx*4+ebp]
	mov	DWORD [16+edi],eax
	xor	eax,DWORD [4+edi]
	mov	DWORD [20+edi],eax
	xor	eax,DWORD [8+edi]
	mov	DWORD [24+edi],eax
	xor	eax,DWORD [12+edi]
	mov	DWORD [28+edi],eax
	inc	ecx
	add	edi,16
	cmp	ecx,10
	jl	NEAR L$01210loop
	mov	DWORD [80+edi],10
	xor	eax,eax
	jmp	NEAR L$010exit
L$00914rounds:
	mov	eax,DWORD [esi]
	mov	ebx,DWORD [4+esi]
	mov	ecx,DWORD [8+esi]
	mov	edx,DWORD [12+esi]
	mov	DWORD [edi],eax
	mov	DWORD [4+edi],ebx
	mov	DWORD [8+edi],ecx
	mov	DWORD [12+edi],edx
	mov	eax,DWORD [16+esi]
	mov	ebx,DWORD [20+esi]
	mov	ecx,DWORD [24+esi]
	mov	edx,DWORD [28+esi]
	mov	DWORD [16+edi],eax
	mov	DWORD [20+edi],ebx
	mov	DWORD [24+edi],ecx
	mov	DWORD [28+edi],edx
	xor	ecx,ecx
	jmp	NEAR L$01314shortcut
align	4
L$01414loop:
	mov	edx,DWORD [28+edi]
L$01314shortcut:
	mov	eax,DWORD [edi]
	movzx	esi,dl
	movzx	ebx,BYTE [esi*1+ebp-128]
	movzx	esi,dh
	shl	ebx,24
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	shr	edx,16
	movzx	esi,dl
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	movzx	esi,dh
	shl	ebx,8
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	shl	ebx,16
	xor	eax,ebx
	xor	eax,DWORD [896+ecx*4+ebp]
	mov	DWORD [32+edi],eax
	xor	eax,DWORD [4+edi]
	mov	DWORD [36+edi],eax
	xor	eax,DWORD [8+edi]
	mov	DWORD [40+edi],eax
	xor	eax,DWORD [12+edi]
	mov	DWORD [44+edi],eax
	cmp	ecx,6
	je	NEAR L$01514break
	inc	ecx
	mov	edx,eax
	mov	eax,DWORD [16+edi]
	movzx	esi,dl
	movzx	ebx,BYTE [esi*1+ebp-128]
	movzx	esi,dh
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	shr	edx,16
	shl	ebx,8
	movzx	esi,dl
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	movzx	esi,dh
	shl	ebx,16
	xor	eax,ebx
	movzx	ebx,BYTE [esi*1+ebp-128]
	shl	ebx,24
	xor	eax,ebx
	mov	DWORD [48+edi],eax
	xor	eax,DWORD [20+edi]
	mov	DWORD [52+edi],eax
	xor	eax,DWORD [24+edi]
	mov	DWORD [56+edi],eax
	xor	eax,DWORD [28+edi]
	mov	DWORD [60+edi],eax
	add	edi,32
	jmp	NEAR L$01414loop
L$01514break:
	mov	DWORD [48+edi],14
	xor	eax,eax
	jmp	NEAR L$010exit
L$006badpointer:
	mov	eax,-1
L$010exit:
	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
global	_GFp_aes_nohw_set_encrypt_key
align	16
_GFp_aes_nohw_set_encrypt_key:
L$_GFp_aes_nohw_set_encrypt_key_begin:
	call	__x86_AES_set_encrypt_key
	ret
db	65,69,83,32,102,111,114,32,120,56,54,44,32,67,82,89
db	80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114
db	111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
segment	.bss
common	_GFp_ia32cap_P 16
