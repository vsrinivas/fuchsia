// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cobalt/cobalt.h"

#include "peridot/lib/cobalt/cobalt.h"

namespace ledger {
namespace {
constexpr int32_t kLedgerCobaltProjectId = 100;
constexpr int32_t kCobaltMetricId = 2;
constexpr int32_t kCobaltEncodingId = 2;

cobalt::CobaltContext* g_cobalt_context = nullptr;

}  // namespace

fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context) {
  return cobalt::InitializeCobalt(task_runner, app_context,
                                  kLedgerCobaltProjectId, kCobaltMetricId,
                                  kCobaltEncodingId, &g_cobalt_context);
}

void ReportEvent(CobaltEvent event) {
  cobalt::ReportEvent(static_cast<uint32_t>(event), g_cobalt_context);
}

}  // namespace ledger
