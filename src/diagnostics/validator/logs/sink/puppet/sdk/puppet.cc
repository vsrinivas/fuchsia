// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/validate/logs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "lib/async/dispatcher.h"
#include "lib/fdio/directory.h"
#include "lib/syslog/structured_backend/cpp/fuchsia_syslog.h"

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

class Puppet : public fuchsia::validate::logs::LogSinkPuppet {
 public:
  explicit Puppet(std::unique_ptr<sys::ComponentContext> context) : context_(std::move(context)) {
    context_->outgoing()->AddPublicService(sink_bindings_.GetHandler(this));
    log_sink_.events().OnRegisterInterest = [=](::fuchsia::diagnostics::Interest interest) {
      if (!interest.has_min_severity()) {
        return;
      }
      min_log_level_ = IntoLogSeverity(interest.min_severity());
      fuchsia_syslog::LogBuffer buffer;

      BeginRecord(&buffer, min_log_level_, __FILE__, __LINE__, "Changed severity",
                  std::nullopt /* condition */);
      buffer.FlushRecord();
    };
    ConnectAsync();
  }
  void EmitPuppetStarted() {
    fuchsia_syslog::LogBuffer buffer;

    BeginRecord(&buffer, FUCHSIA_LOG_INFO, __FILE__, __LINE__, "Puppet started.",
                std::nullopt /* condition */);
    buffer.FlushRecord();
  }

  void BeginRecord(fuchsia_syslog::LogBuffer* buffer, FuchsiaLogSeverity severity,
                   cpp17::optional<cpp17::string_view> file_name, unsigned int line,
                   cpp17::optional<cpp17::string_view> msg,
                   cpp17::optional<cpp17::string_view> condition) {
    buffer->BeginRecord(severity, file_name, line, msg, condition, false, socket_.borrow(), 0,
                        GetKoid(zx_process_self()), GetKoid(zx_thread_self()));
  }

  void BeginRecordPrintf(fuchsia_syslog::LogBuffer* buffer, FuchsiaLogSeverity severity,
                         cpp17::optional<cpp17::string_view> file_name, unsigned int line,
                         cpp17::optional<cpp17::string_view> msg) {
    buffer->BeginRecord(severity, file_name, line, msg, std::nullopt /* condition */, true,
                        socket_.borrow(), 0, GetKoid(zx_process_self()), GetKoid(zx_thread_self()));
  }

  static FuchsiaLogSeverity IntoLogSeverity(fuchsia::diagnostics::Severity severity) {
    switch (severity) {
      case fuchsia::diagnostics::Severity::TRACE:
        return FUCHSIA_LOG_TRACE;
      case fuchsia::diagnostics::Severity::DEBUG:
        return FUCHSIA_LOG_DEBUG;
      case fuchsia::diagnostics::Severity::INFO:
        return FUCHSIA_LOG_INFO;
      case fuchsia::diagnostics::Severity::WARN:
        return FUCHSIA_LOG_WARNING;
      case fuchsia::diagnostics::Severity::ERROR:
        return FUCHSIA_LOG_ERROR;
      case fuchsia::diagnostics::Severity::FATAL:
        return FUCHSIA_LOG_FATAL;
    }
  }

  void ConnectAsync() {
    zx::channel logger, logger_request;
    if (zx::channel::create(0, &logger, &logger_request) != ZX_OK) {
      return;
    }
    // TODO(https://fxbug.dev/75214): Support for custom names.
    if (fdio_service_connect("/svc/fuchsia.logger.LogSink", logger_request.release()) != ZX_OK) {
      return;
    }
    if (log_sink_.Bind(std::move(logger)) != ZX_OK) {
      return;
    }
    zx::socket local, remote;
    if (zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote) != ZX_OK) {
      return;
    }
    log_sink_->ConnectStructured(std::move(remote));
    socket_ = std::move(local);
  }

  void StopInterestListener(StopInterestListenerCallback callback) override {
    min_log_level_ = FUCHSIA_LOG_TRACE;
    // Reconnect per C++ behavior.
    ConnectAsync();
    callback();
  }

  void GetInfo(GetInfoCallback callback) override {
    fuchsia::validate::logs::PuppetInfo info;
    info.pid = GetKoid(zx_process_self());
    info.tid = GetKoid(zx_thread_self());
    callback(info);
  }

  void EmitLog(fuchsia::validate::logs::RecordSpec spec, EmitLogCallback callback) override {
    EmitLog(spec, nullptr);
    callback();
  }

  void EmitPrintfLog(fuchsia::validate::logs::PrintfRecordSpec printf_spec,
                     EmitPrintfLogCallback callback) override {
    EmitLog(printf_spec.record, &printf_spec);
    callback();
  }

  void EmitLog(fuchsia::validate::logs::RecordSpec& spec,
               fuchsia::validate::logs::PrintfRecordSpec* printf_spec) {
    fuchsia_syslog::LogBuffer buffer;
    FuchsiaLogSeverity severity;
    switch (spec.record.severity) {
      case fuchsia::diagnostics::Severity::DEBUG:
        severity = FUCHSIA_LOG_DEBUG;
        break;
      case fuchsia::diagnostics::Severity::ERROR:
        severity = FUCHSIA_LOG_ERROR;
        break;
      case fuchsia::diagnostics::Severity::FATAL:
        severity = FUCHSIA_LOG_FATAL;
        break;
      case fuchsia::diagnostics::Severity::INFO:
        severity = FUCHSIA_LOG_INFO;
        break;
      case fuchsia::diagnostics::Severity::TRACE:
        severity = FUCHSIA_LOG_TRACE;
        break;
      case fuchsia::diagnostics::Severity::WARN:
        severity = FUCHSIA_LOG_WARNING;
        break;
    }
    if (printf_spec) {
      BeginRecordPrintf(&buffer, severity, spec.file.data(), spec.line, printf_spec->msg.data());
      for (auto& arg : printf_spec->printf_arguments) {
        switch (arg.Which()) {
          case fuchsia::validate::logs::PrintfValue::kFloatValue:
            buffer.WriteKeyValue("", arg.float_value());
            break;
          case fuchsia::validate::logs::PrintfValue::kIntegerValue:
            buffer.WriteKeyValue("", arg.integer_value());
            break;
          case fuchsia::validate::logs::PrintfValue::kUnsignedIntegerValue:
            buffer.WriteKeyValue("", arg.unsigned_integer_value());
            break;
          case fuchsia::validate::logs::PrintfValue::kStringValue:
            buffer.WriteKeyValue("", arg.string_value().data());
            break;
          case fuchsia::validate::logs::PrintfValue::Invalid:
            break;
        }
      }
    } else {
      BeginRecord(&buffer, severity, spec.file.data(), spec.line, std::nullopt /* message */,
                  std::nullopt /* condition */);
    }
    for (auto& arg : spec.record.arguments) {
      switch (arg.value.Which()) {
        case fuchsia::diagnostics::stream::Value::Empty:
        case fuchsia::diagnostics::stream::Value::Invalid:
          break;
        case fuchsia::diagnostics::stream::Value::kFloating:
          buffer.WriteKeyValue(arg.name.data(), arg.value.floating());
          break;
        case fuchsia::diagnostics::stream::Value::kSignedInt:
          buffer.WriteKeyValue(arg.name.data(), arg.value.signed_int());
          break;
        case fuchsia::diagnostics::stream::Value::kUnsignedInt:
          buffer.WriteKeyValue(arg.name.data(), arg.value.unsigned_int());
          break;
        case fuchsia::diagnostics::stream::Value::kText:
          buffer.WriteKeyValue(arg.name.data(), arg.value.text().data());
          break;
      }
    }
    if (severity >= min_log_level_) {
      buffer.FlushRecord();
    }
  }

 private:
  FuchsiaLogSeverity min_log_level_ = FUCHSIA_LOG_INFO;
  zx::socket socket_;
  fuchsia::logger::LogSinkPtr log_sink_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::validate::logs::LogSinkPuppet> sink_bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  Puppet puppet(sys::ComponentContext::CreateAndServeOutgoingDirectory());
  puppet.EmitPuppetStarted();
  loop.Run();
}
