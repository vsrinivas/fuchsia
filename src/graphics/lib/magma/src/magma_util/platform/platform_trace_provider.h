// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_TRACE_PROVIDER_H
#define PLATFORM_TRACE_PROVIDER_H

#include <cstdint>
#include <memory>

namespace magma {

class PlatformTraceProvider {
 public:
  virtual ~PlatformTraceProvider() = default;

  // Channel handle always consumed.
  virtual bool Initialize(uint32_t channel) = 0;
  virtual bool IsInitialized() = 0;

  // Returns null if tracing is not enabled.
  static PlatformTraceProvider* Get();

  static std::unique_ptr<PlatformTraceProvider> CreateForTesting();
};

}  // namespace magma

#endif  // PLATFORM_TRACE_PROVIDER_H
