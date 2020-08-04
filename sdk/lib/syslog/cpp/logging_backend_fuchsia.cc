// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/variant.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/logging_backend.h>
#include <lib/syslog/cpp/logging_backend_fuchsia_globals.h>
#include <lib/syslog/streams/cpp/encode.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace syslog_backend {
namespace {

const size_t kMaxTags = 4;  // Legacy from ulib/syslog. Might be worth rethinking.

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

}  // namespace

class LogState {
 public:
  static void Set(const syslog::LogSettings& settings);
  static void Set(const syslog::LogSettings& settings,
                  const std::initializer_list<std::string>& tags);
  static const LogState& Get();

  std::ofstream& file() const {
    static std::ofstream invalid_file;

    if (fit::holds_alternative<std::ofstream>(descriptor_)) {
      return fit::get<std::ofstream>(descriptor_);
    }

    return invalid_file;
  }

  const zx::socket& socket() const {
    static zx::socket invalid;

    if (fit::holds_alternative<zx::socket>(descriptor_)) {
      return fit::get<zx::socket>(descriptor_);
    }

    return invalid;
  }

  zx_koid_t pid() const { return pid_; }

  syslog::LogSeverity min_severity() const { return min_severity_; }

  const std::string& tag_str() const { return tag_str_; }

  const std::string* tags() const { return tags_; }
  size_t num_tags() const { return num_tags_; }

 private:
  LogState(const syslog::LogSettings& settings, const std::initializer_list<std::string>& tags);

  syslog::LogSeverity min_severity_;
  zx_koid_t pid_;
  mutable fit::variant<zx::socket, std::ofstream> descriptor_ = zx::socket();
  std::string tags_[kMaxTags];
  std::string tag_str_;
  size_t num_tags_ = 0;
};

const LogState& LogState::Get() {
  auto state = GetState();

  if (!state) {
    Set(syslog::LogSettings());
    state = GetState();
  }

  return *state;
}

void LogState::Set(const syslog::LogSettings& settings) {
  char process_name[ZX_MAX_NAME_LEN] = "";

  zx_status_t status =
      zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  if (status != ZX_OK)
    process_name[0] = '\0';

  Set(settings, {process_name});
}

void LogState::Set(const syslog::LogSettings& settings,
                   const std::initializer_list<std::string>& tags) {
  if (auto old = SetState(new LogState(settings, tags))) {
    delete old;
  }
}

LogState::LogState(const syslog::LogSettings& settings,
                   const std::initializer_list<std::string>& tags)
    : min_severity_(settings.min_log_level), pid_(GetKoid(zx_process_self())) {
  min_severity_ = settings.min_log_level;

  std::ostringstream tag_str;

  for (auto& tag : tags) {
    if (num_tags_) {
      tag_str << ", ";
    }
    tag_str << tag;
    tags_[num_tags_++] = tag;
    if (num_tags_ >= kMaxTags)
      break;
  }

  tag_str_ = tag_str.str();

  std::ofstream file;
  if (!settings.log_file.empty()) {
    file.open(settings.log_file, std::ios::out | std::ios::app);
  }

  if (file.is_open()) {
    descriptor_ = std::move(file);
  } else {
    zx::channel logger, logger_request;
    if (zx::channel::create(0, &logger, &logger_request) != ZX_OK) {
      return;
    }
    ::fuchsia::logger::LogSink_SyncProxy logger_client(std::move(logger));

    if (fdio_service_connect("/svc/fuchsia.logger.LogSink", logger_request.release()) != ZX_OK) {
      return;
    }
    zx::socket local, remote;
    if (zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote) != ZX_OK) {
      return;
    }

    auto result = logger_client.ConnectStructured(std::move(remote));
    if (result != ZX_OK) {
      return;
    }

    descriptor_ = std::move(local);
  }
}

void SetLogSettings(const syslog::LogSettings& settings) { LogState::Set(settings); }

void SetLogSettings(const syslog::LogSettings& settings,
                    const std::initializer_list<std::string>& tags) {
  LogState::Set(settings, tags);
}

syslog::LogSeverity GetMinLogLevel() { return LogState::Get().min_severity(); }

void WriteLogValue(syslog::LogSeverity severity, const char* file, int line, const char* tag,
                   const char* condition, const syslog::LogValue& msg) {
  WriteLog(severity, file, line, tag, condition, msg.ToString());
}

void WriteLog(syslog::LogSeverity severity, const char* fname, int line, const char* tag,
              const char* condition, const std::string& msg) {
  const LogState& state = LogState::Get();
  std::ostringstream stream;
  zx_time_t time = zx_clock_get_monotonic();
  zx_koid_t pid = state.pid();
  zx_koid_t tid = GetCurrentThreadKoid();

  if (condition)
    stream << "Check failed: " << condition << ". ";

  stream << msg;

  if (auto& file = state.file(); file.is_open()) {
    file << "[" << std::setw(5) << std::setfill('0') << time / 1000000000UL << "." << std::setw(6)
         << (time / 1000UL) % 1000000UL << std::setw(0) << "][" << pid << "][" << tid << "][";

    auto& tag_str = state.tag_str();
    file << tag_str;

    if (tag) {
      if (!tag_str.empty()) {
        file << ", ";
      }

      file << tag;
    }

    file << "] ";

    switch (severity) {
      case syslog::LOG_TRACE:
        file << "TRACE";
        break;
      case syslog::LOG_DEBUG:
        file << "DEBUG";
        break;
      case syslog::LOG_INFO:
        file << "INFO";
        break;
      case syslog::LOG_WARNING:
        file << "WARNING";
        break;
      case syslog::LOG_ERROR:
        file << "ERROR";
        break;
      case syslog::LOG_FATAL:
        file << "FATAL";
        break;
      default:
        file << "VLOG(" << (syslog::LOG_INFO - severity) << ")";
    }

    file << ": [" << fname << "(" << line << ")] " << stream.str() << std::endl;

    // See comment below.
    // TODO(samans)
    if (severity != syslog::LOG_FATAL) {
      return;
    }
  } else if (auto& socket = state.socket()) {
    ::fuchsia::diagnostics::stream::Record record;
    record.severity = ::fuchsia::diagnostics::Severity(severity);
    record.timestamp = time;

    auto& pid_arg = record.arguments.emplace_back();
    pid_arg.name = "pid";
    pid_arg.value.WithUnsignedInt(uint64_t(pid));

    auto& tid_arg = record.arguments.emplace_back();
    tid_arg.name = "tid";
    tid_arg.value.WithUnsignedInt(uint64_t(tid));

    auto& dropped = record.arguments.emplace_back();
    dropped.name = "num_dropped";
    dropped.value.WithUnsignedInt(GetDropped());

    for (size_t i = 0; i < state.num_tags(); i++) {
      auto& tag_arg = record.arguments.emplace_back();
      tag_arg.name = "tag";
      tag_arg.value.WithText(std::string(state.tags()[i]));
    }

    if (tag) {
      auto& tag_arg = record.arguments.emplace_back();
      tag_arg.name = "tag";
      tag_arg.value.WithText(tag);
    }

    auto& msg = record.arguments.emplace_back();
    msg.name = "message";
    msg.value.WithText(stream.str());

    auto& file = record.arguments.emplace_back();
    file.name = "file";
    file.value.WithText(std::string(fname));

    auto& line_arg = record.arguments.emplace_back();
    line_arg.name = "line";
    line_arg.value.WithUnsignedInt(uint64_t(line));

    std::vector<uint8_t> encoded;
    streams::log_record(record, &encoded);

    auto status = socket.write(0, encoded.data(), encoded.size(), nullptr);
    if (status != ZX_OK) {
      IncrementDropped();
    }

    // Write fatal logs to stderr as well because death tests sometimes verify a certain log message
    // was printed prior to the crash.
    // TODO(samans): Convert tests to not depend on stderr. https://fxbug.dev/49593
    if (status != ZX_ERR_BAD_STATE && status != ZX_ERR_PEER_CLOSED &&
        severity != syslog::LOG_FATAL) {
      return;
    }
  }

  std::cerr << stream.str() << std::endl;
}

}  // namespace syslog_backend
