// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cobalt/cobalt.h"

#include <lib/fit/function.h>

#include "peridot/lib/cobalt/cobalt.h"

namespace ledger {
namespace {
constexpr int32_t kLedgerCobaltProjectId = 100;
constexpr int32_t kCobaltMetricId = 2;
constexpr int32_t kCobaltEncodingId = 2;

cobalt::CobaltContext* g_cobalt_context = nullptr;

}  // namespace

fxl::AutoCall<fit::closure> InitializeCobalt(
    async_t* async, fuchsia::sys::StartupContext* context) {
  return cobalt::InitializeCobalt(async, context, kLedgerCobaltProjectId,
                                  &g_cobalt_context);
}

void ReportEvent(CobaltEvent event) {
  fuchsia::cobalt::Value value;
  value.set_index_value(static_cast<uint32_t>(event));
  cobalt::CobaltObservation observation(kCobaltMetricId, kCobaltEncodingId,
                                        std::move(value));
  cobalt::ReportObservation(observation, g_cobalt_context);
}

}  // namespace ledger
