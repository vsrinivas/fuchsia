// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"
#include "setjmp_impl.h"
#include <zircon/tls.h>

ENTRY(setjmp)
END(setjmp)

ALIAS(setjmp, _setjmp)
WEAK_ALIAS(setjmp, sigsetjmp)
