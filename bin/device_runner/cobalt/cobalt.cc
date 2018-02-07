// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/device_runner/cobalt/cobalt.h"

#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "peridot/lib/cobalt/cobalt.h"

namespace modular {
namespace {
constexpr int32_t kCobaltProjectId = 101;
constexpr int32_t kCobaltMetricId = 3;
constexpr int32_t kCobaltEncodingId = 2;

cobalt::CobaltContext* g_cobalt_context = nullptr;

}  // namespace

fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context) {
  return cobalt::InitializeCobalt(task_runner, app_context,
                                  kCobaltProjectId, kCobaltMetricId,
                                  kCobaltEncodingId, &g_cobalt_context);
}

void ReportEvent(CobaltEvent event) {
  cobalt::ReportEvent(static_cast<uint32_t>(event), g_cobalt_context);
}


}  // namespace modular
