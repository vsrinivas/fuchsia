// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "textflag.h"

// func FFS(x uint64) (n uint)
TEXT ·FFS(SB), NOSPLIT, $0
	MOVD x+0(FP), R0
	RBIT R0, R0
	CLZ  R0, R0
	MOVD R0, n+8(FP)
	RET

// func CLZ(x uint64) (n uint)
TEXT ·CLZ(SB), NOSPLIT, $0
	MOVD x+0(FP), R0
	CLZ  R0, R0
	MOVD R0, n+8(FP)
	RET
