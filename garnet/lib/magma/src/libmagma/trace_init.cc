// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_trace_provider.h"

class PlatformTraceInit {
 public:
  PlatformTraceInit() {
    if (magma::PlatformTraceProvider::Get()) {
      magma::PlatformTraceProvider::Get()->Initialize();
    }
  }
} init_platform_trace;
