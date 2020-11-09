// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// The memcpy implementation is verbatim from cortex-strings.

// We include it here so we can define aliases for the sanitizer runtime.  The
// assembler gives warnings if `.weak foo` and `.global foo` both appear,
// though the last one always wins.  To avoid touching the cortex-strings code
// temporarily redefine `global` as a macro; it's only used once in the file.

#define global weak
#include "third_party/lib/cortex-strings/src/aarch64/memcpy.S"
#undef global

.globl __unsanitized_memcpy
.hidden __unsanitized_memcpy
__unsanitized_memcpy = memcpy
