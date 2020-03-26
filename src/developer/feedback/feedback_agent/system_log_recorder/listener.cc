// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/system_log_recorder/listener.h"

#include <fuchsia/logger/cpp/fidl.h>

#include "src/developer/feedback/utils/log_format.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

SystemLogListener::SystemLogListener(std::shared_ptr<sys::ServiceDirectory> services,
                                     LogMessageStore* store)
    : services_(services), store_(store), binding_(this) {}

void SystemLogListener::StartListening() {
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

void SystemLogListener::Log(fuchsia::logger::LogMessage message) {
  store_->Add(Format(std::move(message)));
}

void SystemLogListener::LogMany(std::vector<fuchsia::logger::LogMessage> messages) {
  for (const auto& message : messages) {
    store_->Add(Format(std::move(message)));
  }
}

void SystemLogListener::Done() {
  fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener;

  binding_.Bind(log_listener.NewRequest());

  logger_->Listen(std::move(log_listener), /*options=*/nullptr);
}

}  // namespace feedback
