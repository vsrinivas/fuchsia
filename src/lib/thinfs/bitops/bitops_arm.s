// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "textflag.h"

// func FFS(x uint64) (n uint)
TEXT ·FFS(SB), NOSPLIT, $0
	B ·ffs(SB)

	// func CLZ(x uint64) (n uint)
TEXT ·CLZ(SB), NOSPLIT, $0
	B ·clz(SB)
