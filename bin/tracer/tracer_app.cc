// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>
#include <utility>

#include <mojo/system/main.h>

#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "lib/mtl/tasks/message_loop.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "apps/tracing/services/trace_manager.mojom.h"

namespace tracing {

// TracerApp connects to mojo:tracing and starts trace collection.
// Takes the following command line arguments:
//   --trace-file[=/tmp/trace.json]
//   --duration[=10] in seconds
//   --buffer-size[=2*1024*1024]
//   --categories[=""]
class TracerApp : public mojo::ApplicationImplBase,
                  public mtl::DataPipeDrainer::Client {
 public:
  static constexpr const char* kTraceFileName = "/tmp/trace.json";
  static constexpr uint32_t kBufferSize = 2 * 1024 * 1024;
  static constexpr const char* kCategories = "";
  static constexpr uint64_t kDuration = 10;

  TracerApp() : data_pipe_drainer_(this) {}

 private:
  // |DataPipeDrainer| implementation
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    if (trace_file_.write(static_cast<const char*>(data), num_bytes).fail()) {
      mtl::MessageLoop::GetCurrent()->QuitNow();
    }
  }

  void OnDataComplete() override {
    // TODO(tvoss): Conversion to json should happen here.
    mtl::MessageLoop::GetCurrent()->QuitNow();
  }

  // |ApplicationImplBase| implementation
  void OnInitialize() override {
    command_line_ = ftl::CommandLineFromIteratorsWithArgv0(
        "tracer", args().begin(), args().end());

    auto trace_file_name =
        GetOptionValue<std::string>("trace-file", kTraceFileName);
    auto trace_duration =
        GetOptionValue<uint64_t>("duration", uint64_t(kDuration));
    auto buffer_size = GetOptionValue<size_t>("buffer-size", kBufferSize);
    auto categories = ftl::SplitStringCopy(
        GetOptionValue<std::string>("categories", kCategories), ",",
        ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);

    trace_file_.open(trace_file_name.c_str(), std::ofstream::out |
                                                  std::ofstream::binary |
                                                  std::ofstream::trunc);

    if (!trace_file_.is_open()) {
      FTL_LOG(ERROR) << "Failed to open file for writing: " << trace_file_name;
      mtl::MessageLoop::GetCurrent()->QuitNow();
      return;
    }

    trace_duration_ = std::chrono::seconds(trace_duration);

    ConnectToService(shell(), "mojo:trace_manager",
                     GetProxy(&trace_controller_));
    FTL_DCHECK(trace_controller_) << "Failed to connect to tracing service";

    mx::datapipe_consumer consumer;
    mx::datapipe_producer producer;

    // TODO(tvoss): Define constants for magic values.
    if (mx::datapipe_producer::create(1, buffer_size, 0, &producer, &consumer) <
        0) {
      FTL_LOG(ERROR) << "Failed to create data pipe for trace data collection";
      mtl::MessageLoop::GetCurrent()->QuitNow();
      return;
    }

    data_pipe_drainer_.Start(std::move(consumer));

    // TODO(tvoss): Enable once we switched over to fidl.
    // trace_controller_->StartTracing(
    // mojo::Array<mojo::String>::From(categories),
    // std::move(data_pipe.producer_handle));

    // TODO(tvoss): Replace with a configurable approach to stop
    // tracing cleanly, e.g., after timeout or on user input.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this]() { trace_controller_->StopTracing(); },
        ftl::TimeDelta::FromSeconds(trace_duration_.count()));
  }

  // TODO(tvoss): Factor out to ftl::Commandline as a common helper.
  // Might need the introduction of a custom trait to handle string->type
  // conversion, defaulting to std::istringstream.
  template <typename T>
  T GetOptionValue(const std::string& name, const T& default_value) const {
    size_t idx = 0;
    if (!command_line_.HasOption(name, &idx))
      return default_value;

    std::istringstream ss(command_line_.options()[idx].value);
    T result(default_value);
    ss >> result;

    return result;
  }

  ftl::CommandLine command_line_;
  std::ofstream trace_file_;
  std::chrono::microseconds trace_duration_;
  TraceControllerPtr trace_controller_;
  mtl::DataPipeDrainer data_pipe_drainer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TracerApp);
};

}  // namespace tracing

MojoResult MojoMain(MojoHandle application_request) {
  tracing::TracerApp tracer_app;
  return mojo::RunApplication(application_request, &tracer_app);
}
