// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-libfuzzer.h"
#include "remote.h"

#define REMOTE Remote
#define GET_CALLER_PC() GetRemotePC()

// Generates an implmentation of the __sanitizer_cov_* interface that proxies calls to a process
// running a fuchsia.fuzzer.Proxy FIDL service, but uses fake remote PCs instead of real ones.
#include "sanitizer-cov.inc"

#undef REMOTE
#undef GET_CALLER_PC
