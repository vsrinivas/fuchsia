// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_FAKE_LIBFUZZER_H_
#define SRC_LIB_FUZZING_FIDL_TEST_FAKE_LIBFUZZER_H_

#include <stdint.h>
#include <zircon/compiler.h>

// Expose a symbol to get the remote "PC". This is backed by a global variable
// that can be set by calling |LLVMFuzzerSetRemoteCallerPC|.
__EXPORT uintptr_t GetRemotePC();

#endif  // SRC_LIB_FUZZING_FIDL_TEST_FAKE_LIBFUZZER_H_
