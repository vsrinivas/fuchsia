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
#include "macros.h"

namespace syslog_backend {

using log_word_t = uint64_t;

zx_koid_t GetKoid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

static zx_koid_t pid = GetKoid(zx_process_self());
static thread_local zx_koid_t tid = GetCurrentThreadKoid();

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

template <typename T, size_t size>
static DataSlice<const T> SliceFromArray(const T (&array)[size]) {
  return DataSlice<const T>(array, size);
}

template <size_t size>
static DataSlice<const char> SliceFromArray(const char (&array)[size]) {
  return DataSlice<const char>(array, size - 1);
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

  DataSlice<log_word_t> GetSlice() { return DataSlice<log_word_t>(buffer_, cursor_); }

 private:
  size_t cursor_ = 0;
  log_word_t buffer_[kBufferSize];
};

struct RecordState {
  // Header of the record itself
  uint64_t* header;
  syslog::LogSeverity log_severity;
  ::fuchsia::diagnostics::Severity severity;
  // arg_size in words
  size_t arg_size = 0;
  // key_size in bytes
  size_t current_key_size = 0;
  // Header of the current argument being encoded
  uint64_t* current_header_position = 0;
  uint32_t dropped_count = 0;
  // Current position (in 64-bit words) into the buffer.
  size_t cursor = 0;
  // True if encoding was successful, false otherwise
  bool encode_success = true;
  static RecordState* CreatePtr(LogBuffer* buffer) {
    return reinterpret_cast<RecordState*>(&buffer->record_state);
  }
  size_t PtrToIndex(void* ptr) {
    return reinterpret_cast<size_t>(static_cast<uint8_t*>(ptr)) - reinterpret_cast<size_t>(header);
  }
};
static_assert(sizeof(RecordState) <= sizeof(LogBuffer::record_state));
static_assert(std::alignment_of<RecordState>() == sizeof(uint64_t));

// Used for accessing external data buffers provided by clients.
// Used by the Encoder to do in-place encoding of data
class ExternalDataBuffer {
 public:
  explicit ExternalDataBuffer(LogBuffer* buffer)
      : buffer_(&buffer->data[sizeof(buffer->record_state) / sizeof(log_word_t)]),
        capacity_((sizeof(buffer->data) - sizeof(buffer->record_state))),
        cursor_(RecordState::CreatePtr(buffer)->cursor) {}

  ExternalDataBuffer(log_word_t* data, size_t length, size_t& cursor)
      : buffer_(data), capacity_(length), cursor_(cursor) {}
  __WARN_UNUSED_RESULT bool Write(const log_word_t* data, size_t length) {
    if (cursor_ + length >= capacity_) {
      return false;
    }
    for (size_t i = 0; i < length; i++) {
      buffer_[cursor_ + i] = data[i];
    }
    cursor_ += length;
    return true;
  }

  __WARN_UNUSED_RESULT bool WritePadded(const void* msg, size_t word_count, size_t* written) {
    assert(written != nullptr);
    if (cursor_ + word_count >= capacity_) {
      return false;
    }
    auto retval = WritePaddedInternal(buffer_ + cursor_, msg, word_count);
    cursor_ += retval;
    *written = retval;
    return true;
  }

  template <typename T>
  __WARN_UNUSED_RESULT bool Write(const T& data) {
    static_assert(sizeof(T) >= sizeof(log_word_t));
    static_assert(alignof(T) >= sizeof(log_word_t));
    return Write(reinterpret_cast<const log_word_t*>(&data), sizeof(T) / sizeof(log_word_t));
  }

  uint64_t* data() { return buffer_ + cursor_; }

  DataSlice<log_word_t> GetSlice() { return DataSlice<log_word_t>(buffer_, cursor_); }

 private:
  // Start of buffer
  log_word_t* buffer_ = nullptr;
  // Capacity (in words)
  __UNUSED size_t capacity_ = 0;
  // Current location in buffer (in words)
  size_t& cursor_;
};

template <typename T>
class Encoder {
 public:
  explicit Encoder(T& buffer) { buffer_ = &buffer; }

  void Begin(RecordState& state, zx_time_t timestamp, ::fuchsia::diagnostics::Severity severity) {
    state.severity = severity;
    state.header = buffer_->data();
    log_word_t empty_header = 0;
    state.encode_success &= buffer_->Write(empty_header);
    state.encode_success &= buffer_->Write(timestamp);
  }

  void FlushPreviousArgument(RecordState& state) { state.arg_size = 0; }

  void AppendArgumentKey(RecordState& state, DataSlice<const char> key) {
    FlushPreviousArgument(state);
    auto header_position = buffer_->data();
    log_word_t empty_header = 0;
    state.encode_success &= buffer_->Write(empty_header);
    size_t s_size = 0;
    state.encode_success &= buffer_->WritePadded(key.data(), key.size(), &s_size);
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
    state.encode_success &= buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  void AppendArgumentValue(RecordState& state, uint64_t value) {
    int type = 4;
    state.encode_success &= buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  void AppendArgumentValue(RecordState& state, double value) {
    int type = 5;
    state.encode_success &= buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  void AppendArgumentValue(RecordState& state, DataSlice<const char> string) {
    int type = 6;
    size_t written = 0;
    state.encode_success &= buffer_->WritePadded(string.data(), string.size(), &written);
    state.arg_size += written;
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
// Compiler thinks this is unused even though WriteLogToSocket uses it.
__UNUSED const char kMessageFieldName[] = "message";
const char kPidFieldName[] = "pid";
const char kTidFieldName[] = "tid";
const char kDroppedLogsFieldName[] = "dropped_logs";
const char kTagFieldName[] = "tag";
const char kFileFieldName[] = "file";
const char kLineFieldName[] = "line";

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
  const std::string* tags() const { return tags_; }
  size_t tag_count() const { return num_tags_; }
  // Allowed to be const because descriptor_ is mutable
  fit::variant<zx::socket, std::ofstream>& descriptor() const { return descriptor_; }

 private:
  LogState(const syslog::LogSettings& settings, const std::initializer_list<std::string>& tags);
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

void BeginRecord(LogBuffer* buffer, syslog::LogSeverity severity, const char* file_name,
                 unsigned int line, const char* msg, const char* condition) {
  // Ensure we have log state
  LogState::Get();
  zx_time_t time = zx_clock_get_monotonic();
  auto* state = RecordState::CreatePtr(buffer);
  RecordState& record = *state;
  new (state) RecordState;
  state->log_severity = severity;
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.Begin(*state, time, ::fuchsia::diagnostics::Severity(severity));
  encoder.AppendArgumentKey(record, SliceFromArray(kPidFieldName));
  encoder.AppendArgumentValue(record, static_cast<uint64_t>(pid));
  encoder.AppendArgumentKey(record, SliceFromArray(kTidFieldName));
  encoder.AppendArgumentValue(record, static_cast<uint64_t>(tid));

  auto dropped_count = GetAndResetDropped();
  record.dropped_count = dropped_count;
  if (dropped_count) {
    encoder.AppendArgumentKey(record, SliceFromString(kDroppedLogsFieldName));
    encoder.AppendArgumentValue(record, static_cast<uint64_t>(dropped_count));
  }
  for (size_t i = 0; i < GetState()->tag_count(); i++) {
    encoder.AppendArgumentKey(record, SliceFromString(kTagFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(GetState()->tags()[i]));
  }
  if (msg) {
    encoder.AppendArgumentKey(record, SliceFromString(kMessageFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(msg));
  }

  // TODO(fxbug.dev/56051): Enable this everywhere once doing so won't spam everything.
  if (severity >= syslog::LOG_ERROR) {
    encoder.AppendArgumentKey(record, SliceFromString(kFileFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(file_name));

    encoder.AppendArgumentKey(record, SliceFromString(kLineFieldName));
    encoder.AppendArgumentValue(record, static_cast<uint64_t>(line));
  }
}

void WriteKeyValue(LogBuffer* buffer, const char* key, const char* value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(*state, DataSlice<const char>(key, strlen(key)));
  encoder.AppendArgumentValue(*state, DataSlice<const char>(value, strlen(value)));
}

void WriteKeyValue(LogBuffer* buffer, const char* key, int64_t value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(*state, DataSlice<const char>(key, strlen(key)));
  encoder.AppendArgumentValue(*state, value);
}

void WriteKeyValue(LogBuffer* buffer, const char* key, uint64_t value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(*state, DataSlice<const char>(key, strlen(key)));
  encoder.AppendArgumentValue(*state, value);
}

void WriteKeyValue(LogBuffer* buffer, const char* key, double value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(*state, DataSlice<const char>(key, strlen(key)));
  encoder.AppendArgumentValue(*state, value);
}

void EndRecord(LogBuffer* buffer) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.End(*state);
}

bool FlushRecord(LogBuffer* buffer) {
  auto* state = RecordState::CreatePtr(buffer);
  if (!state->encode_success) {
    return false;
  }
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  auto slice = external_buffer.GetSlice();

  auto& socket = fit::get<zx::socket>(GetState()->descriptor());
  auto status = socket.write(0, slice.data(), slice.size() * sizeof(log_word_t), nullptr);
  if (status != ZX_OK) {
    AddDropped(state->dropped_count + 1);
  }
  return status != ZX_ERR_BAD_STATE && status != ZX_ERR_PEER_CLOSED &&
         state->log_severity != syslog::LOG_FATAL;
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
    : min_severity_(settings.min_log_level), pid_(pid) {
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
                        const char* msg, const char* condition, const T& value) const {
  zx_time_t time = zx_clock_get_monotonic();

  // Cached getter for a stringified version of the log message, so we stringify at most once.
  auto msg_str = [as_str = std::string(), condition, msg, &value]() mutable -> const std::string& {
    if (as_str.empty()) {
      as_str = FullMessageString(condition, msg, value);
    }

    return as_str;
  };

  if (fit::holds_alternative<std::ofstream>(descriptor_)) {
    auto& file = fit::get<std::ofstream>(descriptor_);
    if (WriteLogToFile(&file, time, pid_, tid, severity, file_name, line, nullptr, condition,
                       msg_str())) {
      return;
    }
  } else if (fit::holds_alternative<zx::socket>(descriptor_)) {
    auto& socket = fit::get<zx::socket>(descriptor_);
    std::string message;
    if (msg) {
      message.assign(msg);
    }
    if (WriteLogToSocket(&socket, time, pid_, tid, severity, file_name, line, message, condition,
                         value)) {
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

}  // namespace syslog_backend
