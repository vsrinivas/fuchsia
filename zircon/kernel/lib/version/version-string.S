// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/version/version-string.h>

// This is in assembly so that LTO cannot constant fold this value away.
// Importantly, this data gets filled in later post build so the value
// that LTO see's at link time is not the value that will be here at
// runtime despite this variable being marked const in C++.

.object kVersionString, rodata, global
.space VERSION_STRING_SIZE
.end_object
