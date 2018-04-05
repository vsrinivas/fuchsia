// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COBALT_COBALT_H_
#define PERIDOT_LIB_COBALT_COBALT_H_

#include <set>

#include <fuchsia/cpp/cobalt.h>

#include "garnet/lib/backoff/exponential_backoff.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace cobalt {

class CobaltObservation {
 public:
  CobaltObservation(uint32_t metric_id, uint32_t encoding_id, Value value);
  CobaltObservation(uint32_t metric_id,
                    fidl::VectorPtr<cobalt::ObservationValue> parts);
  CobaltObservation(const CobaltObservation&);
  CobaltObservation(CobaltObservation&&);
  ~CobaltObservation();
  std::string ValueRepr();

  uint32_t metric_id() const { return metric_id_; }
  void Report(CobaltEncoderPtr& encoder,
              std::function<void(Status)> callback) &&;

  CobaltObservation& operator=(const CobaltObservation&);
  CobaltObservation& operator=(CobaltObservation&&);
  bool operator<(const CobaltObservation& rhs) const;

 private:
  bool CompareObservationValueLess(
      const ObservationValue& observationValue,
      const ObservationValue& rhsObservationValue) const;
  uint32_t metric_id_;
  fidl::VectorPtr<cobalt::ObservationValue> parts_;
};

class CobaltContext {
 public:
  CobaltContext(async_t* async, component::ApplicationContext* app_context,
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
  async_t* const async_;
  component::ApplicationContext* app_context_;
  CobaltEncoderPtr encoder_;
  const int32_t project_id_;

  std::multiset<CobaltObservation> observations_to_send_;
  std::multiset<CobaltObservation> observations_in_transit_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltContext);
};

// Cobalt initialization. When cobalt is not need, the returned object must be
// deleted. This method must not be called again until then.
fxl::AutoCall<fxl::Closure> InitializeCobalt(
    async_t* async,
    component::ApplicationContext* app_context, int32_t project_id,
    CobaltContext** cobalt_context);

// Report an observation to Cobalt.
void ReportObservation(CobaltObservation observation,
                       CobaltContext* cobalt_context);

// Report a multipart observation to Cobalt.
void ReportMultipartObservation(uint32_t metric_id,
                                fidl::VectorPtr<cobalt::ObservationValue> parts,
                                CobaltContext* cobalt_context);

};  // namespace cobalt

#endif  // PERIDOT_LIB_COBALT_COBALT_H_
