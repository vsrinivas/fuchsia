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
const char kMessageFieldName[] = "message";
const char kPidFieldName[] = "pid";
const char kTidFieldName[] = "tid";
const char kDroppedLogsFieldName[] = "dropped_logs";
const char kTagFieldName[] = "tag";
const char kFileFieldName[] = "file";
const char kLineFieldName[] = "line";

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

template <typename T>
std::string FullMessageString(const char* condition, const T& msg);

template <>
std::string FullMessageString(const char* condition, const std::string& msg) {
  std::ostringstream stream;

  if (condition)
    stream << "Check failed: " << condition << ". ";

  stream << msg;
  return stream.str();
}

template <>
std::string FullMessageString(const char* condition, const syslog::LogValue& msg) {
  return FullMessageString(condition, msg.ToString());
}

void WriteValueToRecordWithKey(::fuchsia::diagnostics::stream::Record* record,
                               const std::string& key, const syslog::LogValue& value) {
  auto& message = record->arguments.emplace_back();
  message.name = key;

  if (!value) {
    return;
  }

  if (auto string_value = value.string_value()) {
    message.value.WithText(std::string(*string_value));
  } else if (auto int_value = value.int_value()) {
    message.value.WithSignedInt(int64_t(*int_value));
  } else {
    // TODO(fxbug.dev/57571): LogValue also supports lists and nested objects, which Record doesn't.
    // It does NOT support unsigned values, or floats, which Record does.
    message.value.WithText(value.ToString());
  }
}

template <typename T>
void WriteMessageToRecord(::fuchsia::diagnostics::stream::Record* record, const T& msg);

template <>
void WriteMessageToRecord(::fuchsia::diagnostics::stream::Record* record, const std::string& msg) {
  auto& message = record->arguments.emplace_back();
  message.name = kMessageFieldName;
  message.value.WithText(std::string(msg));
}

template <>
void WriteMessageToRecord(::fuchsia::diagnostics::stream::Record* record,
                          const syslog::LogValue& msg) {
  if (auto fields = msg.fields()) {
    for (const auto& field : *fields) {
      WriteValueToRecordWithKey(record, field.key(), field.value());
    }
  } else {
    WriteValueToRecordWithKey(record, "message", msg);
  }
}

}  // namespace

class LogState {
 public:
  static void Set(const syslog::LogSettings& settings);
  static void Set(const syslog::LogSettings& settings,
                  const std::initializer_list<std::string>& tags);
  static const LogState& Get();

  syslog::LogSeverity min_severity() const { return min_severity_; }

  template <typename T>
  void WriteLog(syslog::LogSeverity severity, const char* file_name, int line, const char* tag,
                const char* condition, const T& msg) const;

 private:
  LogState(const syslog::LogSettings& settings, const std::initializer_list<std::string>& tags);

  template <typename T>
  bool WriteLogToSocket(const zx::socket* socket, zx_time_t time, zx_koid_t pid, zx_koid_t tid,
                        syslog::LogSeverity severity, const char* file_name, int line,
                        const char* tag, const char* condition, const T& msg) const;
  bool WriteLogToFile(std::ofstream* file_ptr, zx_time_t time, zx_koid_t pid, zx_koid_t tid,
                      syslog::LogSeverity severity, const char* file_name, int line,
                      const char* tag, const char* condition, const std::string& msg) const;

  syslog::LogSeverity min_severity_;
  zx_koid_t pid_;
  mutable fit::variant<zx::socket, std::ofstream> descriptor_ = zx::socket();
  std::string tags_[kMaxTags];
  std::string tag_str_;
  size_t num_tags_ = 0;
};

template <typename T>
bool LogState::WriteLogToSocket(const zx::socket* socket, zx_time_t time, zx_koid_t pid,
                                zx_koid_t tid, syslog::LogSeverity severity, const char* file_name,
                                int line, const char* tag, const char* condition,
                                const T& msg) const {
  ::fuchsia::diagnostics::stream::Record record;
  record.severity = ::fuchsia::diagnostics::Severity(severity);
  record.timestamp = time;

  auto& pid_arg = record.arguments.emplace_back();
  pid_arg.name = kPidFieldName;
  pid_arg.value.WithUnsignedInt(uint64_t(pid));

  auto& tid_arg = record.arguments.emplace_back();
  tid_arg.name = kTidFieldName;
  tid_arg.value.WithUnsignedInt(uint64_t(tid));

  auto dropped_count = GetAndResetDropped();

  if (dropped_count) {
    auto& dropped = record.arguments.emplace_back();
    dropped.name = kDroppedLogsFieldName;
    dropped.value.WithUnsignedInt(dropped_count);
  }

  for (size_t i = 0; i < num_tags_; i++) {
    auto& tag_arg = record.arguments.emplace_back();
    tag_arg.name = kTagFieldName;
    tag_arg.value.WithText(std::string(tags_[i]));
  }

  if (tag) {
    auto& tag_arg = record.arguments.emplace_back();
    tag_arg.name = kTagFieldName;
    tag_arg.value.WithText(tag);
  }

  // TODO(fxbug.dev/56051): Enable this everywhere once doing so won't spam everything.
  if (severity >= syslog::LOG_ERROR) {
    auto& file = record.arguments.emplace_back();
    file.name = kFileFieldName;
    file.value.WithText(std::string(file_name));

    auto& line_arg = record.arguments.emplace_back();
    line_arg.name = kLineFieldName;
    line_arg.value.WithUnsignedInt(uint64_t(line));
  }

  WriteMessageToRecord(&record, msg);

  std::vector<uint8_t> encoded;
  streams::log_record(record, &encoded);

  auto status = socket->write(0, encoded.data(), encoded.size(), nullptr);
  if (status != ZX_OK) {
    AddDropped(dropped_count + 1);
  }

  return status != ZX_ERR_BAD_STATE && status != ZX_ERR_PEER_CLOSED &&
         severity != syslog::LOG_FATAL;
}

bool LogState::WriteLogToFile(std::ofstream* file_ptr, zx_time_t time, zx_koid_t pid, zx_koid_t tid,
                              syslog::LogSeverity severity, const char* file_name, int line,
                              const char* tag, const char* condition,
                              const std::string& msg) const {
  auto& file = *file_ptr;
  file << "[" << std::setw(5) << std::setfill('0') << time / 1000000000UL << "." << std::setw(6)
       << (time / 1000UL) % 1000000UL << std::setw(0) << "][" << pid << "][" << tid << "][";

  auto& tag_str = tag_str_;
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

  file << ": [" << file_name << "(" << line << ")] " << msg << std::endl;

  return severity != syslog::LOG_FATAL;
}

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

template <typename T>
void LogState::WriteLog(syslog::LogSeverity severity, const char* file_name, int line,
                        const char* tag, const char* condition, const T& msg) const {
  zx_koid_t tid = GetCurrentThreadKoid();
  zx_time_t time = zx_clock_get_monotonic();

  // Cached getter for a stringified version of the log message, so we stringify at most once.
  auto msg_str = [as_str = std::string(), condition, &msg]() mutable -> const std::string& {
    if (as_str.empty()) {
      as_str = FullMessageString(condition, msg);
    }

    return as_str;
  };

  if (fit::holds_alternative<std::ofstream>(descriptor_)) {
    auto& file = fit::get<std::ofstream>(descriptor_);
    if (WriteLogToFile(&file, time, pid_, tid, severity, file_name, line, tag, condition,
                       msg_str())) {
      return;
    }
  } else if (fit::holds_alternative<zx::socket>(descriptor_)) {
    auto& socket = fit::get<zx::socket>(descriptor_);
    if (WriteLogToSocket(&socket, time, pid_, tid, severity, file_name, line, tag, condition,
                         msg)) {
      return;
    }
  }

  std::cerr << msg_str() << std::endl;
}

void SetLogSettings(const syslog::LogSettings& settings) { LogState::Set(settings); }

void SetLogSettings(const syslog::LogSettings& settings,
                    const std::initializer_list<std::string>& tags) {
  LogState::Set(settings, tags);
}

syslog::LogSeverity GetMinLogLevel() { return LogState::Get().min_severity(); }

void WriteLogValue(syslog::LogSeverity severity, const char* file_name, int line, const char* tag,
                   const char* condition, const syslog::LogValue& msg) {
  LogState::Get().WriteLog(severity, file_name, line, tag, condition, msg);
}

void WriteLog(syslog::LogSeverity severity, const char* file_name, int line, const char* tag,
              const char* condition, const std::string& msg) {
  LogState::Get().WriteLog(severity, file_name, line, tag, condition, msg);
}

}  // namespace syslog_backend
