// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_trace.h"

class PlatformTraceInit {
 public:
  PlatformTraceInit() {
    if (magma::PlatformTrace::Get()) {
      magma::PlatformTrace::Get()->Initialize();
    }
  }
} init_platform_trace;
