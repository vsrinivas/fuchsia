// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COBALT_COBALT_H_
#define PERIDOT_LIB_COBALT_COBALT_H_

#include "lib/app/cpp/application_context.h"
#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/lib/backoff/exponential_backoff.h"

namespace cobalt {

class CobaltContext {
 public:
  CobaltContext(fxl::RefPtr<fxl::TaskRunner> task_runner,
                app::ApplicationContext* app_context,
                int32_t project_id,
                int32_t metric_id,
                int32_t encoding_id);
  ~CobaltContext();

  void ReportEvent(uint32_t event);

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void ReportEventOnMainThread(uint32_t event);
  void SendEvents();

  backoff::ExponentialBackoff backoff_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  app::ApplicationContext* app_context_;
  cobalt::CobaltEncoderPtr encoder_;
  const int32_t project_id_;
  const int32_t metric_id_;
  const int32_t encoding_id_;

  std::multiset<uint32_t> events_to_send_;
  std::multiset<uint32_t> events_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltContext);
};

// Cobalt initialization. When cobalt is not need, the returned object must be
// deleted. This method must not be called again until then.
fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context,
    int32_t project_id,
    int32_t metric_id,
    int32_t encoding_id,
    CobaltContext** cobalt_context);

// Report an event to Cobalt.
void ReportEvent(uint32_t event, CobaltContext* cobalt_context);

};  // namespace cobalt

#endif  // PERIDOT_LIB_COBALT_COBALT_H_
