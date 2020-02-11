// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/tests/stub_logger.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include <cstdint>

#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

constexpr zx::duration kLogMessageBaseTimestamp = zx::sec(15604);
constexpr uint64_t kLogMessageProcessId = 7559;
constexpr uint64_t kLogMessageThreadId = 7687;

}  // namespace

fuchsia::logger::LogMessage BuildLogMessage(const int32_t severity, const std::string& text,
                                            const zx::duration timestamp_offset,
                                            const std::vector<std::string>& tags) {
  fuchsia::logger::LogMessage msg{};
  msg.time = (kLogMessageBaseTimestamp + timestamp_offset).get();
  msg.pid = kLogMessageProcessId;
  msg.tid = kLogMessageThreadId;
  msg.tags = tags;
  msg.severity = severity;
  msg.msg = text;
  return msg;
}

void StubLogger::Listen(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                        std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  fuchsia::logger::LogListenerPtr log_listener_ptr = log_listener.Bind();
  FXL_CHECK(log_listener_ptr.is_bound());
  for (const auto& message : messages_) {
    log_listener_ptr->Log(message);
  }
}

void StubLogger::DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                          std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  fuchsia::logger::LogListenerPtr log_listener_ptr = log_listener.Bind();
  FXL_CHECK(log_listener_ptr.is_bound());
  log_listener_ptr->LogMany(messages_);
  log_listener_ptr->Done();
}

void StubLoggerClosesConnection::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  CloseAllConnections();
}

void StubLoggerNeverBindsToLogListener::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {}

void StubLoggerUnbindsFromLogListenerAfterOneMessage::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  FXL_CHECK(messages_.size() > 1u)
      << "You need to set up more than one message using set_messages()";

  fuchsia::logger::LogListenerPtr log_listener_ptr = log_listener.Bind();
  FXL_CHECK(log_listener_ptr.is_bound());
  log_listener_ptr->LogMany(
      std::vector<fuchsia::logger::LogMessage>(messages_.begin(), messages_.begin() + 1));
  log_listener_ptr.Unbind();
}

void StubLoggerNeverCallsLogManyBeforeDone::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  fuchsia::logger::LogListenerPtr log_listener_ptr = log_listener.Bind();
  FXL_CHECK(log_listener_ptr.is_bound());
  log_listener_ptr->Done();
}

void StubLoggerBindsToLogListenerButNeverCalls::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  log_listener_ptr_ = log_listener.Bind();
  FXL_CHECK(log_listener_ptr_.is_bound());
}

void StubLoggerDelaysAfterOneMessage::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  FXL_CHECK(messages_.size() > 1u)
      << "You need to set up more than one message using set_messages()";

  fuchsia::logger::LogListenerPtr log_listener_ptr = log_listener.Bind();
  FXL_CHECK(log_listener_ptr.is_bound());
  log_listener_ptr->LogMany(
      std::vector<fuchsia::logger::LogMessage>(messages_.begin(), messages_.begin() + 1));

  FX_LOGS(INFO) << "Stub logger delaying the remaining messages for " << delay_.to_msecs() << "ms";
  FXL_CHECK(async::PostDelayedTask(
                dispatcher_,
                [this, log_listener_ptr = std::move(log_listener_ptr)] {
                  log_listener_ptr->LogMany(std::vector<fuchsia::logger::LogMessage>(
                      messages_.begin() + 1, messages_.end()));
                  log_listener_ptr->Done();
                },
                delay_) == ZX_OK);
}

void StubLoggerDelayedResponses::Listen(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  log_listener_ptr_ = log_listener.Bind();
  size_t i = 0;
  for (const auto& message : messages_) {
    async::PostDelayedTask(
        dispatcher_, [this, message]() { log_listener_ptr_->Log(message); },
        delay_between_responses_ * i);
    ++i;
  }
}

void StubLoggerDelayedResponses::DumpLogs(
    fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) {
  log_listener_ptr_ = log_listener.Bind();

  size_t i = 0;
  for (const auto& dump : dumps_) {
    async::PostDelayedTask(
        dispatcher_, [this, dump]() { log_listener_ptr_->LogMany(dump); },
        delay_between_responses_ * i);
    ++i;
  }

  async::PostDelayedTask(
      dispatcher_, [this]() { log_listener_ptr_->Done(); }, delay_between_responses_ * i);
}

zx::duration StubLoggerDelayedResponses::TotalDelayBetweenDumps() {
  return delay_between_responses_ * (dumps_.size());
}

zx::duration StubLoggerDelayedResponses::TotalDelayBetweenMessages() {
  return delay_between_responses_ * (messages_.size() - 1);
}

}  // namespace feedback
