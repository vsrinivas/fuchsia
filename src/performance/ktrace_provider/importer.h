// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_KTRACE_PROVIDER_IMPORTER_H_
#define SRC_PERFORMANCE_KTRACE_PROVIDER_IMPORTER_H_

#include <lib/trace-engine/context.h>
#include <stddef.h>
#include <stdint.h>

#include <array>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fbl/string.h>

#include "src/performance/ktrace_provider/reader.h"
#include "src/performance/ktrace_provider/tags.h"

namespace ktrace_provider {

class Reader;

class Importer {
 public:
  static constexpr zx_koid_t kNoProcess = 0u;

  static constexpr zx_koid_t kKernelPseudoKoidBase = 0x00000000'70000000u;
  static constexpr zx_koid_t kKernelPseudoCpuBase = kKernelPseudoKoidBase + 0x00000000'01000000u;

  Importer(trace_context* context);
  ~Importer();

  bool Import(Reader& reader);

 private:
  trace_context_t* const context_;

  Importer(const Importer&) = delete;
  Importer(Importer&&) = delete;
  Importer& operator=(const Importer&) = delete;
  Importer& operator=(Importer&&) = delete;
};

}  // namespace ktrace_provider

#endif  // SRC_PERFORMANCE_KTRACE_PROVIDER_IMPORTER_H_
