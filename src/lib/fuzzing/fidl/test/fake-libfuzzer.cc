// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-libfuzzer.h"

#include <zircon/compiler.h>

#include "libfuzzer.h"

static uintptr_t gRemotePC = 0;

__EXPORT void LLVMFuzzerSetRemoteCallerPC(uintptr_t pc) { gRemotePC = pc; }

__EXPORT uintptr_t GetRemotePC() { return gRemotePC; }
