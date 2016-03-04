// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "textflag.h"

// func FFS(x uint64) (n uint)
TEXT ·FFS(SB),NOSPLIT,$0
	BSFQ	x+0(FP), AX
	MOVQ	AX, n+8(FP)
	RET

// func CLZ(x uint64) (n uint)
TEXT ·CLZ(SB),NOSPLIT,$0
	BSRQ	x+0(FP), AX
	MOVQ	$63, BX
	SUBQ	AX, BX
	MOVQ	BX, n+8(FP)
	RET
