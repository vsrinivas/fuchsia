// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_TRACE_APP_H_
#define APPS_TRACING_SRC_TRACE_TRACE_APP_H_

#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/tracing/lib/trace_converters/chromium_exporter.h"
#include "apps/tracing/services/trace_controller.fidl.h"
#include "apps/tracing/src/trace/configuration.h"
#include "apps/tracing/src/trace/tracer.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace tracing {

class TraceApp {
 public:
  explicit TraceApp(Configuration configuration);
  ~TraceApp();

 private:
  void ListCategories();
  void ListProviders();
  void StartTrace();
  void StopTrace();
  void DoneTrace();

  Configuration configuration_;
  std::unique_ptr<modular::ApplicationContext> context_;
  TraceControllerPtr trace_controller_;

  modular::ApplicationControllerPtr application_controller_;

  std::unique_ptr<ChromiumExporter> exporter_;
  std::unique_ptr<Tracer> tracer_;
  bool tracing_ = false;

  ftl::WeakPtrFactory<TraceApp> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceApp);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_TRACE_APP_H_
