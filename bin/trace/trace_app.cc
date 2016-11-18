// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/trace_app.h"

#include <fstream>
#include <sstream>
#include <utility>

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

TraceApp::TraceApp(Configuration configuration)
    : configuration_(std::move(configuration)),
      context_(modular::ApplicationContext::CreateFromStartupInfo()),
      trace_controller_(
          context_->ConnectToEnvironmentService<TraceController>()),
      weak_ptr_factory_(this) {
  FTL_DCHECK(context_);

  trace_controller_.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "Lost connection to trace controller";
    exit(1);
  });

  if (configuration_.list_providers)
    ListProviders();
  else
    StartTrace();
}

TraceApp::~TraceApp() {}

void TraceApp::ListProviders() {
  trace_controller_->GetRegisteredProviders(
      [](fidl::Array<TraceProviderInfoPtr> providers) {
        std::ostringstream out;
        out << "Registered providers" << std::endl;
        for (const auto& provider : providers) {
          out << "  #" << provider->id << ": '" << provider->label << "'"
              << std::endl;
        }
        FTL_LOG(INFO) << out.str();
        mtl::MessageLoop::GetCurrent()->QuitNow();
      });
}

void TraceApp::StartTrace() {
  std::ofstream out(configuration_.output_file_name,
                    std::ios_base::out | std::ios_base::trunc);
  if (!out.is_open()) {
    FTL_LOG(ERROR) << "Failed to open " << configuration_.output_file_name
                   << " for writing";
    exit(1);
  }

  exporter_.reset(new ChromiumExporter(std::move(out)));
  tracer_.reset(new Tracer(trace_controller_.get()));

  FTL_LOG(INFO) << "Starting trace; will stop in "
                << configuration_.duration.ToSecondsF() << " seconds...";
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          weak->StopTrace();
      },
      configuration_.duration);

  tracing_ = true;
  tracer_->Start(
      std::move(configuration_.categories),
      [this](const reader::Record& record) { exporter_->ExportRecord(record); },
      [](std::string error) { FTL_LOG(ERROR) << error; },
      [weak = weak_ptr_factory_.GetWeakPtr()] {
        if (weak)
          weak->DoneTrace();
      });

  if (configuration_.launch_info) {
    FTL_LOG(INFO) << "Launching " << configuration_.launch_info->url;
    context_->launcher()->CreateApplication(
        std::move(configuration_.launch_info),
        GetProxy(&application_controller_));
    application_controller_.set_connection_error_handler([this] {
      // TODO(jeffbrown): We might want to offer an option to continue tracing
      // even when the launched program terminates.
      FTL_LOG(INFO) << "Application terminated, stopping trace";
      StopTrace();
    });
  }
}

void TraceApp::StopTrace() {
  if (tracing_) {
    FTL_LOG(INFO) << "Stopping trace...";
    tracing_ = false;
    tracer_->Stop();
  }
}

void TraceApp::DoneTrace() {
  tracer_.reset();
  exporter_.reset();

  FTL_LOG(INFO) << "Trace file written to " << configuration_.output_file_name;
  mtl::MessageLoop::GetCurrent()->QuitNow();
}

}  // namespace tracing
