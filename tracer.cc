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
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/utility/run_loop.h"
#include "mojo/services/tracing/interfaces/tracing.mojom.h"

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
  static constexpr const uint64_t kDuration = 10;

  TracerApp() = default;

 private:
  // |DataPipeDrainer| implementation
  void OnDataAvailable(const void* data, size_t num_bytes) override {
    if (trace_file_.write(static_cast<const char*>(data), num_bytes).fail()) {
      mojo::RunLoop::current()->Quit();
      return;
    }
  }

  void OnDataComplete() override { mojo::RunLoop::current()->Quit(); }

  // |ApplicationImplBase| implementation
  void OnInitialize() override {
    command_line_ = ftl::CommandLineFromIteratorsWithArgv0(
        "tracer", args().begin(), args().end());

    auto trace_file_name =
        GetOptionValue<std::string>("trace-file", kTraceFileName);
    auto trace_duration =
        GetOptionValue<uint64_t>("duration", uint64_t(kDuration));
    auto buffer_size = GetOptionValue<size_t>("buffer-size", kBufferSize);
    auto categories = GetOptionValue<std::string>("categories", kCategories);

    trace_file_.open(trace_file_name.c_str(), std::ofstream::out |
                                                  std::ofstream::binary |
                                                  std::ofstream::trunc);

    if (!trace_file_.is_open()) {
      FTL_LOG(ERROR) << "Failed to open file for writing: " << trace_file_name;
      mojo::RunLoop::current()->Quit();
      return;
    }

    trace_duration_ = std::chrono::seconds(trace_duration);

    ConnectToService(shell(), "mojo:tracing", GetProxy(&trace_collector_));
    FTL_DCHECK(trace_collector_) << "Failed to connect to tracing service";

    MojoCreateDataPipeOptions options;
    options.struct_size = sizeof(MojoCreateDataPipeOptions);
    options.flags = MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE;
    options.element_num_bytes = 1;
    options.capacity_num_bytes = buffer_size;

    mojo::DataPipe data_pipe(options);
    data_pipe_drainer_.reset(
        new mtl::DataPipeDrainer(this, std::move(data_pipe.consumer_handle)));

    trace_collector_->Start(std::move(data_pipe.producer_handle),
                            mojo::String(categories));

    // TODO(tvoss): Replace with a configurable approach to stop
    // tracing cleanly, e.g., after timeout or on user input.
    mojo::RunLoop::current()->PostDelayedTask(
        [this]() { trace_collector_->StopAndFlush(); },
        trace_duration_.count());
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
  TraceCollectorPtr trace_collector_;
  std::unique_ptr<mtl::DataPipeDrainer> data_pipe_drainer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TracerApp);
};

}  // namespace tracing

MojoResult MojoMain(MojoHandle application_request) {
  tracing::TracerApp tracer;
  return mojo::RunApplication(application_request, &tracer);
}
