// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request.h>
#include <zircon/compiler.h>

// Offers a symbol that can be called from rust code to generate a backtrace
// request.
__ALWAYS_INLINE void backtrace_request_for_rust(void) { backtrace_request(); }
