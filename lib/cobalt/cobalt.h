// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COBALT_COBALT_H_
#define PERIDOT_LIB_COBALT_COBALT_H_

#include <memory>
#include <string>
#include <utility>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fxl/functional/auto_call.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/ref_ptr.h>

namespace cobalt {

class CobaltObservation {
 public:
  CobaltObservation(uint32_t metric_id, uint32_t encoding_id,
                    fuchsia::cobalt::Value value);
  CobaltObservation(uint32_t metric_id,
                    fidl::VectorPtr<fuchsia::cobalt::ObservationValue> parts);
  CobaltObservation(const CobaltObservation&);
  CobaltObservation(CobaltObservation&&) noexcept;
  ~CobaltObservation();
  std::string ValueRepr();

  uint32_t metric_id() const { return metric_id_; }
  void Report(fuchsia::cobalt::CobaltEncoderPtr& encoder,
              fit::function<void(fuchsia::cobalt::Status)> callback) &&;

  CobaltObservation& operator=(const CobaltObservation&);
  CobaltObservation& operator=(CobaltObservation&&) noexcept;
  bool operator<(const CobaltObservation& rhs) const;

 private:
  bool CompareObservationValueLess(
      const fuchsia::cobalt::ObservationValue& observationValue,
      const fuchsia::cobalt::ObservationValue& rhsObservationValue) const;
  uint32_t metric_id_;
  fidl::VectorPtr<fuchsia::cobalt::ObservationValue> parts_;
};

class CobaltContext {
 public:
  virtual ~CobaltContext() {}

  // Reports an observation to Cobalt.
  virtual void ReportObservation(CobaltObservation observation) = 0;
};

// Returns a CobaltContext initialized with the provided parameters.
std::unique_ptr<CobaltContext> MakeCobaltContext(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    int32_t project_id);

// Returns a CobaltContext initialized with the provided parameters.
std::unique_ptr<CobaltContext> MakeCobaltContext(
    async_dispatcher_t* dispatcher, component::StartupContext* context,
    fsl::SizedVmo config);

// Cobalt initialization. When cobalt is not needed anymore, the returned object
// must be deleted. This method must not be called again until then.
// DEPRECATED - prefer MakeCobaltContext().
fxl::AutoCall<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, component::StartupContext* startup_context,
    int32_t project_id, CobaltContext** cobalt_context);

// Cobalt initialization. When cobalt is not needed anymore, the returned object
// must be deleted. This method must not be called again until then.
// DEPRECATED - prefer MakeCobaltContext().
fxl::AutoCall<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, component::StartupContext* startup_context,
    fsl::SizedVmo config, CobaltContext** cobalt_context);

// Reports an observation to Cobalt if |cobalt_context| is not nullptr.
// DEPRECATED - prefer calling CobaltContext::ReportObservation directly
// (testing the pointer only if necessary).
void ReportObservation(CobaltObservation observation,
                       CobaltContext* cobalt_context);
};  // namespace cobalt

#endif  // PERIDOT_LIB_COBALT_COBALT_H_
