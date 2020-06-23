// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_LOGGER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_LOGGER_H_

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl_test_base.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

// Returns a LogMessage with the given severity, message and optional tags.
//
// The process and thread ids are constants. The timestamp is a constant plus the optionally
// provided offset.
fuchsia::logger::LogMessage BuildLogMessage(const int32_t severity, const std::string& text,
                                            const zx::duration timestamp_offset = zx::duration(0),
                                            const std::vector<std::string>& tags = {});

using LoggerBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::logger, Log);

class Logger : public LoggerBase {
 public:
  // |fuchsia:logger::Log|
  void Listen(::fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
              std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override {
    FX_NOTREACHED();
  }

  void DumpLogs(::fidl::InterfaceHandle<fuchsia::logger::LogListener> log_listener,
                std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override {
    FX_NOTREACHED();
  }

  void ListenSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                  std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  void DumpLogsSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  //  Injection methods.
  void set_messages(const std::vector<fuchsia::logger::LogMessage>& messages) {
    messages_ = messages;
  }

 private:
  std::vector<fuchsia::logger::LogMessage> messages_;
};

class LoggerClosesConnection : public LoggerBase {
 public:
  // |fuchsia:logger::Log|
  STUB_METHOD_CLOSES_CONNECTION(ListenSafe,
                                ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe>,
                                std::unique_ptr<fuchsia::logger::LogFilterOptions>);

  STUB_METHOD_CLOSES_CONNECTION(DumpLogsSafe,
                                ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe>,
                                std::unique_ptr<fuchsia::logger::LogFilterOptions>);
};

class LoggerNeverBindsToLogListener : public LoggerBase {
 public:
  // |fuchsia:logger::Log|
  STUB_METHOD_DOES_NOT_RETURN(ListenSafe, ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe>,
                              std::unique_ptr<fuchsia::logger::LogFilterOptions>);

  STUB_METHOD_DOES_NOT_RETURN(DumpLogsSafe,
                              ::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe>,
                              std::unique_ptr<fuchsia::logger::LogFilterOptions>);
};

class LoggerUnbindsFromLogListenerAfterOneMessage : public LoggerBase {
 public:
  // |fuchsia:logger::Log|
  void DumpLogsSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  //  Injection methods.
  void set_messages(const std::vector<fuchsia::logger::LogMessage>& messages) {
    messages_ = messages;
  }

 private:
  std::vector<fuchsia::logger::LogMessage> messages_;
};

class LoggerNeverCallsLogManyBeforeDone : public LoggerBase {
 public:
  // |fuchsia:logger::Log|
  void DumpLogsSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
};

class LoggerBindsToLogListenerButNeverCalls : public LoggerBase {
 public:
  // |fuchsia:logger::Log|
  void DumpLogsSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

 private:
  // Owns the connection with the log listener so that it doesn't get closed when DumpLogs()
  // returns and we can test the timeout on the log listener side.
  fuchsia::logger::LogListenerSafePtr log_listener_ptr_;
};

class LoggerDelaysAfterOneMessage : public LoggerBase {
 public:
  LoggerDelaysAfterOneMessage(async_dispatcher_t* dispatcher, zx::duration delay)
      : dispatcher_(dispatcher), delay_(delay) {}

  // |fuchsia:logger::Log|
  void DumpLogsSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  //  Injection methods.
  void set_messages(const std::vector<fuchsia::logger::LogMessage>& messages) {
    messages_ = messages;
  }

 private:
  async_dispatcher_t* dispatcher_;
  zx::duration delay_;
  std::vector<fuchsia::logger::LogMessage> messages_;
};

class LoggerDelayedResponses : public LoggerBase {
 public:
  LoggerDelayedResponses(async_dispatcher_t* dispatcher,
                         std::vector<std::vector<fuchsia::logger::LogMessage>> dumps,
                         std::vector<fuchsia::logger::LogMessage> messages,
                         zx::duration delay_between_responses)
      : dispatcher_(dispatcher),
        dumps_(dumps),
        messages_(messages),
        delay_between_responses_(delay_between_responses) {}

  // |fuchsia:logger::Log|
  void ListenSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                  std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;
  void DumpLogsSafe(::fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> log_listener,
                    std::unique_ptr<fuchsia::logger::LogFilterOptions> options) override;

  zx::duration TotalDelayBetweenDumps();
  zx::duration TotalDelayBetweenMessages();

 private:
  async_dispatcher_t* dispatcher_;

  std::vector<std::vector<fuchsia::logger::LogMessage>> dumps_;
  std::vector<fuchsia::logger::LogMessage> messages_;
  zx::duration delay_between_responses_;

  fuchsia::logger::LogListenerSafePtr log_listener_ptr_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_LOGGER_H_
