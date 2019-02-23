// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// The strcpy implementation is verbatim from cortex-strings.
// But for userboot we need to make the symbol hidden.

#include "third_party/lib/cortex-strings/src/aarch64/strcpy.S"

#ifdef HIDDEN
    .hidden strcpy
#endif
