// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// The strchrnul implementation is verbatim from cortex-strings.
// But the strchrnul symbol must be weak, with a __strchrnul alias.

#define global weak
#include "third_party/lib/cortex-strings/src/aarch64/strchrnul.S"
#undef global

ALIAS(strchrnul, __strchrnul)
