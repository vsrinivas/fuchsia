// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sanitizer-cov-proxy.h"

#define SANITIZER_COV_PROXY SanitizerCovProxy
#define GET_CALLER_PC() __builtin_return_address(0)

// Generates an implmentation of the __sanitizer_cov_* interface that proxies calls to a process
// running a fuchsia.fuzzer.Coverage FIDL service.
#include "sanitizer-cov.inc"

#undef SANITIZER_COV_PROXY
#undef GET_CALLER_PC
