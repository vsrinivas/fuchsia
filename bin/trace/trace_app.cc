// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>
#include <utility>

#include "apps/tracing/lib/trace/trace_reader.h"
#include "apps/tracing/lib/trace_converters/chromium_exporter.h"
#include "apps/tracing/src/trace/trace_app.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

namespace {

static constexpr uint32_t kEmptyOptions = 0;

}  // namespace

TraceApp::TraceApp(Configuration configuration)
    : context_(modular::ApplicationContext::CreateFromStartupInfo()),
      trace_controller_(
          context_->ConnectToEnvironmentService<TraceController>()),
      buffer_(configuration.buffer_size),
      output_file_name_(configuration.output_file_name) {
  FTL_DCHECK(context_);

  if (!temp_dir_.NewTempFile(&buffer_file_name_)) {
    FTL_LOG(ERROR) << "Failed to create temporary file";
    exit(1);
  }

  FTL_LOG(INFO) << "Buffering binary trace data to " << buffer_file_name_;

  // TODO(tvoss): Revisit and get rid of the temporary file for
  // buffering incoming binary trace data. Instead, we should read
  // chunks directly off the underlying transport.
  buffer_file_.open(buffer_file_name_, std::ios_base::out |
                                           std::ios_base::trunc |
                                           std::ios_base::binary);

  if (!buffer_file_.is_open()) {
    FTL_LOG(ERROR) << "Failed to open " << buffer_file_name_ << " for writing";
    exit(1);
  }

  mx_status_t status = NO_ERROR;
  mx::socket outgoing_socket;
  if ((status = mx::socket::create(kEmptyOptions, &socket_, &outgoing_socket)) <
      0) {
    FTL_LOG(ERROR) << "Failed to create socket: " << status;
    exit(1);
  }

  mtl::MessageLoop::GetCurrent()->AddHandler(
      this, socket_.get(), MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED);

  trace_controller_->StartTracing(
      fidl::Array<fidl::String>::From(configuration.categories),
      std::move(outgoing_socket));

  FTL_LOG(INFO) << "Tracing started, scheduling stop in: "
                << configuration.duration.ToSeconds() << " [s]";

  if (configuration.launch_info)
    LaunchApp(std::move(configuration.launch_info));

  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      [this]() {
        trace_controller_->StopTracing();
        FTL_LOG(INFO) << "Tracing requested to stop";
      },
      configuration.duration);
}

void TraceApp::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (pending & MX_SIGNAL_READABLE) {
    mx_status_t status = NO_ERROR;
    mx_size_t actual = 0;
    if ((status = socket_.read(kEmptyOptions, buffer_.data(), buffer_.size(),
                               &actual)) != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to read data from socket: " << status;
      return;
    }

    buffer_file_.write(buffer_.data(), actual);
    if (buffer_file_.bad() || buffer_file_.fail()) {
      FTL_LOG(ERROR) << "Failed to write trace data";
      exit(1);
    }
  }

  if (pending & MX_SIGNAL_PEER_CLOSED) {
    ExportTrace();
    application_controller_.reset();
    exit(0);
  }
}

void TraceApp::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_LOG(ERROR) << "Error on socket: " << error;
  exit(1);
}

void TraceApp::ExportTrace() {
  FTL_LOG(INFO) << "Exporting data to: " << output_file_name_;

  buffer_file_.close();

  std::ofstream out(output_file_name_,
                    std::ios_base::out | std::ios_base::trunc);
  if (!out.is_open()) {
    FTL_LOG(ERROR) << "Failed to open " << output_file_name_ << " for writing";
    return;
  }

  std::ifstream in(buffer_file_name_,
                   std::ios_base::in | std::ios_base::binary);
  if (!in.is_open()) {
    FTL_LOG(ERROR) << "Failed to open " << buffer_file_name_ << " for reading";
    return;
  }

  ChromiumExporter exporter(out);

  reader::StreamTraceInput input(in);
  reader::TraceReader reader(
      [](const std::string& error) { FTL_LOG(ERROR) << error; },
      [&exporter](const reader::Record& record) {
        exporter.ExportRecord(record);
      });

  reader.ForEachRecord(input);
}

void TraceApp::LaunchApp(modular::ApplicationLaunchInfoPtr info) {
  context_->launcher()->CreateApplication(std::move(info),
                                          GetProxy(&application_controller_));
}

}  // namespace tracing
