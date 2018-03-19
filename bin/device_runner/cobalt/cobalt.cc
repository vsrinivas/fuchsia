// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/device_runner/cobalt/cobalt.h"

#include "lib/cobalt/fidl/cobalt.fidl.h"
#include "peridot/lib/cobalt/cobalt.h"

namespace modular {
namespace {
constexpr int32_t kCobaltProjectId = 101;
constexpr int32_t kCobaltNoOpEncodingId = 2;

cobalt::CobaltContext* g_cobalt_context = nullptr;

}  // namespace

fxl::AutoCall<fxl::Closure> InitializeCobalt(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    component::ApplicationContext* app_context) {
  return cobalt::InitializeCobalt(task_runner, app_context,
                                  kCobaltProjectId, &g_cobalt_context);
}

void ReportEvent(ModularEvent event) {
  cobalt::ValuePtr value = cobalt::Value::New();
  value->set_index_value(static_cast<uint32_t>(event));
  cobalt::CobaltObservation observation(
      static_cast<uint32_t>(CobaltMetric::MODULAR_EVENTS),
      kCobaltNoOpEncodingId, std::move(value));
  cobalt::ReportObservation(observation, g_cobalt_context);
}

void ReportModuleLaunchTime(std::string module_url, zx_time_t time_nanos) {
 auto parts = f1dl::VectorPtr<cobalt::ObservationValuePtr>::New(2);
 const int64_t time_micros = static_cast<int64_t>(time_nanos / ZX_USEC(1));

 parts->at(0) = cobalt::ObservationValue::New();
 parts->at(0)->name = "module_url";
 parts->at(0)->encoding_id = kCobaltNoOpEncodingId;
 parts->at(0)->value = cobalt::Value::New();
 parts->at(0)->value->set_string_value(module_url);

 parts->at(1) = cobalt::ObservationValue::New();
 parts->at(1)->name = "launch_time_micros";
 parts->at(1)->encoding_id = kCobaltNoOpEncodingId;
 parts->at(1)->value = cobalt::Value::New();
 parts->at(1)->value->set_int_value(time_micros);

 cobalt::CobaltObservation observation(
     static_cast<uint32_t>(CobaltMetric::MODULE_LAUNCH_LATENCY), std::move(parts));
 cobalt::ReportObservation(observation, g_cobalt_context);
}

void ReportStoryLaunchTime(zx_time_t time_nanos) {
  const int64_t time_micros = static_cast<int64_t>(time_nanos / ZX_USEC(1));
  cobalt::ValuePtr value = cobalt::Value::New();
  value->set_int_value(time_micros);
  cobalt::CobaltObservation observation(
      static_cast<uint32_t>(CobaltMetric::STORY_LAUNCH_LATENCY),
      kCobaltNoOpEncodingId, std::move(value));
  cobalt::ReportObservation(observation, g_cobalt_context);
}

}  // namespace modular
