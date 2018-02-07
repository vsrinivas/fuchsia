// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_COBALT_COBALT_H_
#define PERIDOT_BIN_DEVICE_RUNNER_COBALT_COBALT_H_

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace modular {

// The events to report.
// Next enum value: 2
enum class CobaltEvent : uint32_t {
  BOOTED_TO_DEVICE_RUNNER = 0,
  BOOTED_TO_USER_RUNNER = 1,
};

// Cobalt initialization. When cobalt is not need, the returned object must be
// deleted. This method must not be called again until then.
fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context);

// Report an event to Cobalt.
void ReportEvent(CobaltEvent event);

};  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_COBALT_COBALT_H_
