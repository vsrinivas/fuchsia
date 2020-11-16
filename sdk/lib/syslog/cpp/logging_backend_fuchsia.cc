// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/diagnostics/stream/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/optional.h>
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

#include "lib/syslog/streams/cpp/fields.h"
#include "logging_backend_fuchsia_private.h"

namespace syslog_backend {
namespace {

using log_word_t = uint64_t;

// Represents a slice of a buffer of type T.
template <typename T>
class DataSlice {
 public:
  DataSlice(T* ptr, size_t size) : ptr_(ptr), size_(size) {}

  T& operator[](size_t offset) {
    assert(offset < size_);
    return ptr_[offset];
  }

  const T& operator[](size_t offset) const {
    assert(offset < size_);
    return ptr_[offset];
  }

  size_t size() { return size_; }

  T* data() { return ptr_; }

 private:
  T* ptr_;
  size_t size_;
};

static DataSlice<const char> SliceFromString(const std::string& string) {
  return DataSlice<const char>(string.data(), string.size());
}

static constexpr int WORD_SIZE = sizeof(log_word_t);  // See sdk/lib/syslog/streams/cpp/encode.cc
class DataBuffer {
 public:
  static constexpr auto kBufferSize = (1 << 15) / WORD_SIZE;
  void Write(const log_word_t* data, size_t length) {
    assert(cursor_ + length < kBufferSize);
    for (size_t i = 0; i < length; i++) {
      buffer_[cursor_ + i] = data[i];
    }
    cursor_ += length;
  }

  size_t WritePadded(const void* msg, size_t length) {
    assert(cursor_ + length < kBufferSize);
    auto retval = WritePaddedInternal(buffer_ + cursor_, msg, length);
    cursor_ += retval;
    return retval;
  }

  template <typename T>
  void Write(const T& data) {
    static_assert(sizeof(T) >= sizeof(log_word_t));
    static_assert(alignof(T) >= sizeof(log_word_t));
    Write(reinterpret_cast<const log_word_t*>(&data), sizeof(T) / sizeof(log_word_t));
  }

  uint64_t* data() { return buffer_ + cursor_; }

  DataSlice<log_word_t> GetSlice() { return DataSlice<log_word_t>(buffer_, cursor_); }

 private:
  size_t cursor_ = 0;
  log_word_t buffer_[kBufferSize];
};

struct RecordState {
  // Header of the record itself
  uint64_t* header;
  ::fuchsia::diagnostics::Severity severity;
  // arg_size in words
  size_t arg_size = 0;
  // key_size in bytes
  size_t current_key_size = 0;
  // Header of the current argument being encoded
  uint64_t* current_header_position = 0;
  size_t PtrToIndex(void* ptr) {
    return reinterpret_cast<size_t>(static_cast<uint8_t*>(ptr)) - reinterpret_cast<size_t>(header);
  }
};

template <typename T>
class Encoder {
 public:
  explicit Encoder(T& buffer) { buffer_ = &buffer; }

  RecordState Begin(zx_time_t timestamp, ::fuchsia::diagnostics::Severity severity) {
    RecordState state;
    state.severity = severity;
    state.header = buffer_->data();
    log_word_t empty_header = 0;
    buffer_->Write(empty_header);
    buffer_->Write(timestamp);
    return state;
  }

  void FlushPreviousArgument(RecordState& state) { state.arg_size = 0; }

  void AppendArgumentKey(RecordState& state, DataSlice<const char> key) {
    FlushPreviousArgument(state);
    auto header_position = buffer_->data();
    log_word_t empty_header = 0;
    buffer_->Write(empty_header);
    size_t s_size = buffer_->WritePadded(key.data(), key.size());
    state.arg_size = s_size + 1;  // offset by 1 for the header
    state.current_key_size = key.size();
    state.current_header_position = header_position;
  }

  uint64_t ComputeArgHeader(RecordState& state, int type, uint64_t value_ref = 0) {
    return ArgumentFields::Type::Make(type) | ArgumentFields::SizeWords::Make(state.arg_size) |
           ArgumentFields::NameRefVal::Make(state.current_key_size) |
           ArgumentFields::NameRefMSB::Make(state.current_key_size > 0 ? 1 : 0) |
           ArgumentFields::ValueRef::Make(value_ref) | ArgumentFields::Reserved::Make(0);
  }

  void AppendArgumentValue(RecordState& state, int64_t value) {
    int type = 3;
    buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  void AppendArgumentValue(RecordState& state, uint64_t value) {
    int type = 4;
    buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  void AppendArgumentValue(RecordState& state, double value) {
    int type = 5;
    buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  void AppendArgumentValue(RecordState& state, DataSlice<const char> string) {
    int type = 6;
    state.arg_size += buffer_->WritePadded(string.data(), string.size());
    *state.current_header_position =
        ComputeArgHeader(state, type, string.size() > 0 ? (1 << 15) | string.size() : 0);
  }

  void End(RecordState& state) {
    // See src/lib/diagnostics/stream/rust/src/lib.rs
    constexpr auto kTracingFormatLogRecordType = 9;
    FlushPreviousArgument(state);
    uint64_t header =
        HeaderFields::Type::Make(kTracingFormatLogRecordType) |
        HeaderFields::SizeWords::Make(static_cast<size_t>(buffer_->data() - state.header)) |
        HeaderFields::Reserved::Make(0) | HeaderFields::Severity::Make(state.severity);
    *state.header = header;
  }

 private:
  T* buffer_;
};

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

void WriteValueToRecordWithKeyDeprecated(RecordState& record, Encoder<DataBuffer>& encoder,
                                         const std::string& key, const syslog::LogValue& value) {
  encoder.AppendArgumentKey(record, SliceFromString(key));

  if (!value) {
    return;
  }

  if (auto string_value = value.string_value()) {
    encoder.AppendArgumentValue(record, SliceFromString(*string_value));
  } else if (auto int_value = value.int_value()) {
    encoder.AppendArgumentValue(record, int64_t(*int_value));
  } else {
    // TODO(fxbug.dev/57571): LogValue also supports lists and nested objects, which Record doesn't.
    // It does NOT support unsigned values, or floats, which Record does.
    encoder.AppendArgumentValue(record, SliceFromString(value.ToString()));
  }
}

template <typename T>
void WriteMessageToRecordDeprecated(RecordState& state, Encoder<T>& encoder,
                                    const std::string& msg) {
  encoder.AppendArgumentKey(state, SliceFromString(kMessageFieldName));
  encoder.AppendArgumentValue(state, SliceFromString(msg));
}

template <typename T>
void WriteMessageToRecordDeprecated(RecordState& state, Encoder<T>& encoder,
                                    const syslog::LogValue& msg) {
  if (auto fields = msg.fields()) {
    for (const auto& field : *fields) {
      WriteValueToRecordWithKeyDeprecated(state, encoder, field.key(), field.value());
    }
  } else {
    WriteValueToRecordWithKeyDeprecated(state, encoder, "message", msg);
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
  void WriteLog(syslog::LogSeverity severity, const char* file_name, unsigned int line,
                const char* tag, const char* condition, const T& msg) const;

 private:
  LogState(const syslog::LogSettings& settings, const std::initializer_list<std::string>& tags);

  template <typename T>
  bool WriteLogToSocket(const zx::socket* socket, zx_time_t time, zx_koid_t pid, zx_koid_t tid,
                        syslog::LogSeverity severity, const char* file_name, unsigned int line,
                        const char* tag, const char* condition, const T& msg) const;
  bool WriteLogToFile(std::ofstream* file_ptr, zx_time_t time, zx_koid_t pid, zx_koid_t tid,
                      syslog::LogSeverity severity, const char* file_name, unsigned int line,
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
                                unsigned int line, const char* tag, const char* condition,
                                const T& msg) const {
  std::unique_ptr<DataBuffer> buffer = std::make_unique<DataBuffer>();
  Encoder<DataBuffer> encoder(*buffer);
  auto record = encoder.Begin(time, ::fuchsia::diagnostics::Severity(severity));
  encoder.AppendArgumentKey(record, SliceFromString(kPidFieldName));
  encoder.AppendArgumentValue(record, uint64_t(pid));
  encoder.AppendArgumentKey(record, SliceFromString(kTidFieldName));
  encoder.AppendArgumentValue(record, uint64_t(tid));

  auto dropped_count = GetAndResetDropped();
  if (dropped_count) {
    encoder.AppendArgumentKey(record, SliceFromString(kDroppedLogsFieldName));
    encoder.AppendArgumentValue(record, uint64_t(dropped_count));
  }

  for (size_t i = 0; i < num_tags_; i++) {
    encoder.AppendArgumentKey(record, SliceFromString(kTagFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(tags_[i]));
  }

  if (tag) {
    encoder.AppendArgumentKey(record, SliceFromString(kTagFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(tag));
  }

  // TODO(fxbug.dev/56051): Enable this everywhere once doing so won't spam everything.
  if (severity >= syslog::LOG_ERROR) {
    encoder.AppendArgumentKey(record, SliceFromString(kFileFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(file_name));

    encoder.AppendArgumentKey(record, SliceFromString(kLineFieldName));
    encoder.AppendArgumentValue(record, uint64_t(line));
  }

  WriteMessageToRecordDeprecated(record, encoder, msg);  // See inline below
  encoder.End(record);
  auto slice = buffer->GetSlice();
  auto status = socket->write(0, slice.data(), slice.size() * sizeof(log_word_t), nullptr);
  if (status != ZX_OK) {
    AddDropped(dropped_count + 1);
  }

  return status != ZX_ERR_BAD_STATE && status != ZX_ERR_PEER_CLOSED &&
         severity != syslog::LOG_FATAL;
}

bool LogState::WriteLogToFile(std::ofstream* file_ptr, zx_time_t time, zx_koid_t pid, zx_koid_t tid,
                              syslog::LogSeverity severity, const char* file_name,
                              unsigned int line, const char* tag, const char* condition,
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

void LogState::Set(const syslog::LogSettings& settings) { Set(settings, {}); }

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
void LogState::WriteLog(syslog::LogSeverity severity, const char* file_name, unsigned int line,
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

void WriteLogValue(syslog::LogSeverity severity, const char* file_name, unsigned int line,
                   const char* tag, const char* condition, const syslog::LogValue& msg) {
  LogState::Get().WriteLog(severity, file_name, line, tag, condition, msg);
}

void WriteLog(syslog::LogSeverity severity, const char* file_name, unsigned int line,
              const char* tag, const char* condition, const std::string& msg) {
  LogState::Get().WriteLog(severity, file_name, line, tag, condition, msg);
}

}  // namespace syslog_backend
