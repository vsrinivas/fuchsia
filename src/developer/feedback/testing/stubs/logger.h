// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_LOGGER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_LOGGER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

namespace feedback {
namespace stubs {

// Returns a LogMessage with the given severity, message and optional tags.
//
// The process and thread ids are constants. The timestamp is a constant plus the optionally
// provided offset.
fuchsia::logger::LogMessage BuildLogMessage(const int32_t severity, const std::string& text,
                                            const zx::duration timestamp_offset = zx::duration(0),
                                            const std::vector<std::string>& tags = {});

//  Log service to return canned responses to Log::DumpLogs().
class Logger : public fuchsia::logger::Log {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::logger::Log> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::logger::Log> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::logger::Log>>(this, std::move(request));
    };
  }

  // |fuchsia::logger::Log|.
  void Listen(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
              std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  //  injection methods.
  void set_messages(const std::vector<fuchsia::logger::LogMessage>& messages) {
    messages_ = messages;
  }

  void CloseConnection();

 protected:
  std::unique_ptr<fidl::Binding<fuchsia::logger::Log>> binding_;
  std::vector<fuchsia::logger::LogMessage> messages_;
};

class LoggerClosesConnection : public Logger {
 public:
  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
};

class LoggerNeverBindsToLogListener : public Logger {
 public:
  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
};

class LoggerUnbindsFromLogListenerAfterOneMessage : public Logger {
 public:
  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
};

class LoggerNeverCallsLogManyBeforeDone : public Logger {
 public:
  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
};

class LoggerBindsToLogListenerButNeverCalls : public Logger {
 public:
  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

 private:
  // Owns the connection with the log listener so that it doesn't get closed when DumpLogs()
  // returns and we can test the timeout on the log listener side.
  fuchsia::logger::LogListenerPtr log_listener_ptr_;
};

class LoggerDelaysAfterOneMessage : public Logger {
 public:
  LoggerDelaysAfterOneMessage(async_dispatcher_t* dispatcher, zx::duration delay)
      : dispatcher_(dispatcher), delay_(delay) {}

  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

 private:
  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
};

class LoggerDelayedResponses : public Logger {
 public:
  LoggerDelayedResponses(async_dispatcher_t* dispatcher,
                         std::vector<std::vector<fuchsia::logger::LogMessage>> dumps,
                         std::vector<fuchsia::logger::LogMessage> messages,
                         zx::duration delay_between_responses)
      : dispatcher_(dispatcher),
        dumps_(dumps),
        messages_(messages),
        delay_between_responses_(delay_between_responses) {}

  void Listen(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
              std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
  void DumpLogs(fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  zx::duration TotalDelayBetweenDumps();
  zx::duration TotalDelayBetweenMessages();

 private:
  async_dispatcher_t* dispatcher_;

  std::vector<std::vector<fuchsia::logger::LogMessage>> dumps_;
  std::vector<fuchsia::logger::LogMessage> messages_;
  zx::duration delay_between_responses_;

  fuchsia::logger::LogListenerPtr log_listener_ptr_;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_LOGGER_H_
