// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_LIBFUZZER_H_
#define SRC_LIB_FUZZING_FIDL_LIBFUZZER_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

extern "C" {

__EXPORT void LLVMFuzzerSetRemoteCallerPC(uintptr_t pc);
__EXPORT int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

}  // extern "C"

#endif  // SRC_LIB_FUZZING_FIDL_LIBFUZZER_H_
