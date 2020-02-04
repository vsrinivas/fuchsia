// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_trace.h"

namespace magma {

std::unique_ptr<PlatformTraceObserver> PlatformTraceObserver::Create() { return nullptr; }

uint64_t PlatformTrace::GetCurrentTicks() { return 0; }

}  // namespace magma
