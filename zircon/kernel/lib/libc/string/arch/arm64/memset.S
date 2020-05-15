// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// The memset implementation is verbatim from cortex-strings.
// It's a different implementation than the userland memset because vector
// registers cannot be used in the kernel.

// We include it here so we can define aliases for the sanitizer runtime.

#include "third_party/lib/cortex-strings/no-neon/src/aarch64/memset.S"

.weak memset

.globl __unsanitized_memset
.hidden __unsanitized_memset
__unsanitized_memset = memset
