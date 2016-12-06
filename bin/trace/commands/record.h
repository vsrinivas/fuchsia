// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_COMMANDS_RECORD_H_
#define APPS_TRACING_SRC_TRACE_COMMANDS_RECORD_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/modular/services/application/application_controller.fidl.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/tracing/lib/trace_converters/chromium_exporter.h"
#include "apps/tracing/src/trace/command.h"
#include "apps/tracing/src/trace/tracer.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/time/time_delta.h"

namespace tracing {

class Record : public CommandWithTraceController {
 public:
  struct Options {
    bool Setup(const ftl::CommandLine&);

    std::string output_file_name = "/tmp/trace.json";
    ftl::TimeDelta duration = ftl::TimeDelta::FromSeconds(10);
    std::vector<std::string> categories = {};
    bool detach = false;
    bool decouple = false;
    uint32_t buffer_size_megabytes_hint = 4;
    modular::ApplicationLaunchInfoPtr launch_info;
  };

  static Info Describe();

  explicit Record(modular::ApplicationContext* context);
  void Run(const ftl::CommandLine& command_line) override;

 private:
  void StopTrace();
  void DoneTrace();
  void LaunchApp();
  void StartTimer();

  modular::ApplicationControllerPtr application_controller_;
  std::unique_ptr<ChromiumExporter> exporter_;
  std::unique_ptr<Tracer> tracer_;
  bool tracing_ = false;
  Options options_;

  ftl::WeakPtrFactory<Record> weak_ptr_factory_;
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_COMMANDS_RECORD_H_
