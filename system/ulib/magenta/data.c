// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

#include <magenta/compiler.h>

// This must be allocated in the image and not go into .bss.
// The compiler uses .bss for things initialized to all zero
// even when they're const, so give it a nonzero initializer
// to force it into the proper .rodata section.
const struct vdso_constants DATA_CONSTANTS = { 0xdeadbeef };
