// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/zx/time.h>

#include <cinttypes>
#include <thread>

#include <trace/event.h>

#include "src/developer/feedback/utils/log_format.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

constexpr size_t kMaxLogLinesInQueue = 512u;

}  // namespace

SystemLogRecorder::SystemLogRecorder(std::shared_ptr<sys::ServiceDirectory> services,
                                     const std::vector<const std::string>& file_paths,
                                     FileSize total_log_size)
    : services_(services),
      binding_(this),
      queue_(kMaxLogLinesInQueue),
      logs_(file_paths, total_log_size) {}

void SystemLogRecorder::StartRecording() {
  StartListening();
  SpawnWriterThread();
}

void SystemLogRecorder::StartListening() {
  fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener;

  binding_.Bind(log_listener.NewRequest());
  binding_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.logger.LogListener";
  });

  logger_ = services_->Connect<fuchsia::logger::Log>();
  logger_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.logger.Log";
  });

  // We first ask the logger to send all of the logs it has cached and then we begin listening for
  // new log messages. It's possible that we could be missing messages the logger receives between
  // when it calls Done() and our call to Listen().
  logger_->DumpLogs(std::move(log_listener), /*options=*/nullptr);
}

void SystemLogRecorder::SpawnWriterThread() {
  std::thread([this] {
    while (true) {
      TRACE_DURATION("feedback:io", "SystemLogRecorder::write_task");
      logs_.Write(Format(queue_.Pop()));
    }
  }).detach();
}

void SystemLogRecorder::LogMany(std::vector<fuchsia::logger::LogMessage> messages) {
  for (const auto& message : messages) {
    TRACE_DURATION("feedback:io", "SystemLogRecorder::LogManyPush", "message_size",
                   message.msg.size());
    queue_.Push(std::move(message));
  }
}

void SystemLogRecorder::Log(fuchsia::logger::LogMessage message) {
  TRACE_DURATION("feedback:io", "SystemLogRecorder::Log", "message_size", message.msg.size());
  queue_.Push(std::move(message));
}

void SystemLogRecorder::Done() {
  fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener;

  binding_.Bind(log_listener.NewRequest());
  binding_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "Lost connection to fuchsia.logger.LogListener";
  });

  logger_->Listen(std::move(log_listener), /*options=*/nullptr);
}

}  // namespace feedback
