// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "textflag.h"

// func FFS(x uint64) (n uint)
TEXT ·FFS(SB), NOSPLIT, $0
	BSFQ x+0(FP), AX
	MOVQ AX, n+8(FP)
	RET

// func CLZ(x uint64) (n uint)
TEXT ·CLZ(SB), NOSPLIT, $0
	BSRQ x+0(FP), AX
	MOVQ $63, BX
	SUBQ AX, BX
	MOVQ BX, n+8(FP)
	RET
