// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/listener.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

SystemLogListener::SystemLogListener(std::shared_ptr<sys::ServiceDirectory> services,
                                     LogMessageStore* store)
    : services_(services), store_(store), binding_(this) {}

void SystemLogListener::StartListening() {
  ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener;

  binding_.Bind(log_listener.NewRequest());
  binding_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.logger.LogListener";
  });

  logger_ = services_->Connect<fuchsia::logger::Log>();
  logger_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(WARNING, status) << "Lost connection to fuchsia.logger.Log";
  });

  // We first ask the logger to send all of the logs it has cached and then we begin listening for
  // new log messages. It's possible that we could be missing messages the logger receives between
  // when it calls Done() and our call to Listen().
  logger_->DumpLogsSafe(std::move(log_listener), /*options=*/nullptr);
}

void SystemLogListener::Log(fuchsia::logger::LogMessage message, LogCallback received) {
  store_->Add(std::move(message));
  received();
}

void SystemLogListener::LogMany(std::vector<fuchsia::logger::LogMessage> messages,
                                LogManyCallback received) {
  for (const auto& message : messages) {
    store_->Add(std::move(message));
  }
  received();
}

void SystemLogListener::Done() {
  ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener;

  binding_.Bind(log_listener.NewRequest());

  logger_->ListenSafe(std::move(log_listener), /*options=*/nullptr);
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
