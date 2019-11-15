// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_TRACE_PROVIDER_WITH_FDIO_H
#define PLATFORM_TRACE_PROVIDER_WITH_FDIO_H

#include "platform_trace_provider.h"

namespace magma {

bool InitializeTraceProviderWithFdio(PlatformTraceProvider* provider);

}  // namespace magma

#endif  // PLATFORM_TRACE_PROVIDER_WITH_FDIO_H
