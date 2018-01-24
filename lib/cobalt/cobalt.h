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

class CobaltObservation {
 public:
  CobaltObservation(uint32_t metric_id, uint32_t encoding_id, ValuePtr value);
  CobaltObservation(const CobaltObservation&);
  CobaltObservation(CobaltObservation&&);
  ~CobaltObservation();
  std::string ValueRepr();

  uint32_t encoding_id() const { return encoding_id_; }
  uint32_t metric_id() const { return metric_id_; }
  const ValuePtr& value() const { return value_; }

  CobaltObservation& operator=(const CobaltObservation&);
  CobaltObservation& operator=(CobaltObservation&&);
  bool operator<(const CobaltObservation& rhs) const;

 private:
  uint32_t encoding_id_;
  uint32_t metric_id_;
  ValuePtr value_;
};

class CobaltContext {
 public:
  CobaltContext(fxl::RefPtr<fxl::TaskRunner> task_runner,
                app::ApplicationContext* app_context,
                int32_t project_id);
  ~CobaltContext();

  void ReportObservation(CobaltObservation observation);

 private:
  void ConnectToCobaltApplication();
  void OnConnectionError();
  void ReportObservationOnMainThread(CobaltObservation observation);
  void SendObservations();
  void AddObservationCallback(CobaltObservation observation, Status status);

  backoff::ExponentialBackoff backoff_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  app::ApplicationContext* app_context_;
  CobaltEncoderPtr encoder_;
  const int32_t project_id_;

  std::multiset<CobaltObservation> observations_to_send_;
  std::multiset<CobaltObservation> observations_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltContext);
};

// Cobalt initialization. When cobalt is not need, the returned object must be
// deleted. This method must not be called again until then.
fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    app::ApplicationContext* app_context,
    int32_t project_id,
    CobaltContext** cobalt_context);

// Report an observation to Cobalt.
void ReportObservation(CobaltObservation observation,
                       CobaltContext* cobalt_context);

};  // namespace cobalt

#endif  // PERIDOT_LIB_COBALT_COBALT_H_
