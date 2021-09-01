// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/stdcompat/variant.h>
#include <lib/sync/completion.h>
#include <lib/syslog/structured_backend/cpp/fuchsia_syslog.h>
#include <lib/syslog/structured_backend/fuchsia_syslog.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/time.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

// Represents a byte offset that has no alignment guarantees.
class ByteOffset final {
 public:
  ByteOffset(const ByteOffset& other) = default;
  ByteOffset(ByteOffset&& other) = default;
  static ByteOffset FromBuffer(size_t offset, size_t capacity) {
    return ByteOffset(offset, capacity).AssertValid();
  }

  static ByteOffset Unbounded(size_t offset) { return ByteOffset(offset, -1); }

  size_t unsafe_get() const { return value_; }

  size_t capacity() const { return capacity_; }

  ByteOffset AssertAlignedTo(size_t size) const {
    assert((value_ % size) == 0);
    if (!((value_ % size) == 0)) {
      abort();
    }
    return *this;
  }

  ByteOffset& operator=(ByteOffset&& other) = default;

  ByteOffset& operator=(const ByteOffset& other) = default;

  ByteOffset operator+(size_t offset) const {
    return ByteOffset(value_ + offset, capacity_).AssertValid();
  }

 private:
  ByteOffset(size_t value, size_t capacity) : value_(value), capacity_(capacity) {}

  ByteOffset& AssertValid() {
    if (!((capacity_ == 0) && (value_ == 0))) {
      assert(value_ < capacity_);
      if (!(value_ < capacity_)) {
        abort();
      }
    }
    return *this;
  }

  size_t value_;
  size_t capacity_;
};

}  // namespace

namespace fuchsia_syslog {

// A word offset, which guarantees that the pointer is
// aligned to alignof(T). Operations are done with respect
// to words, not bytes.
template <typename T>
class WordOffset final {
 public:
  WordOffset() = delete;
  WordOffset& operator=(const WordOffset& other) = default;
  WordOffset(const WordOffset& other) {
    capacity_ = other.capacity_;
    value_ = other.value_;
  }

  WordOffset operator+(size_t offset) const {
    return WordOffset(value_ + offset, capacity_).AssertValid();
  }

  WordOffset operator+(WordOffset offset) const {
    return WordOffset(value_ + offset.value_, capacity_).AssertValid();
  }

  WordOffset operator++(int) const { return WordOffset(value_ + 1, capacity_).AssertValid(); }

  WordOffset AddPadded(const ByteOffset& byte_offset) {
    size_t needs_padding = (byte_offset.unsafe_get() % sizeof(T)) > 0;
    // Multiply by needs_padding to set padding to 0 if no padding
    // is necessary. This avoids unnecessary branching.
    return WordOffset(value_ + (byte_offset.unsafe_get() / sizeof(T)) + needs_padding, capacity_);
  }

  WordOffset begin() { return WordOffset(0, capacity_); }

  size_t capacity() { return capacity_; }

  static WordOffset FromByteOffset(const ByteOffset& value) {
    return WordOffset(value.AssertAlignedTo(sizeof(T)).unsafe_get() / sizeof(T),
                      value.capacity() / sizeof(T));
  }

  size_t unsafe_get() const { return value_; }

  bool in_bounds(WordOffset<T> offset) { return !((offset.value_ + value_) >= capacity_); }

  ByteOffset ToByteOffset() const {
    return ByteOffset::FromBuffer(value_ * sizeof(T), capacity_ * sizeof(T));
  }

  void reset() { value_ = 0; }

  static WordOffset invalid() { return WordOffset(0, 0); }

 private:
  WordOffset(size_t value, size_t capacity) : capacity_(capacity), value_(value) {}

  WordOffset& AssertValid() {
    if (!((capacity_ == 0) && (value_ == 0))) {
      if (!(value_ < capacity_)) {
        abort();
      }
      assert(value_ < capacity_);
    }
    return *this;
  }

  size_t capacity_;
  size_t value_;
};

template <typename T>
WordOffset<T> WritePaddedInternal(T* buffer, const void* msg, const ByteOffset& length) {
  size_t needs_padding = (length.unsafe_get() % sizeof(T)) > 0;
  size_t padding = sizeof(T) - (length.unsafe_get() % sizeof(T));
  // Multiply by needs_padding to set padding to 0 if no padding
  // is necessary. This avoids unnecessary branching.
  padding *= needs_padding;
  // If we added padding -- zero the padding bytes in a single write operation
  size_t is_nonzero_length = length.unsafe_get() != 0;
  size_t eof_in_bytes = length.unsafe_get() + padding;
  size_t eof_in_words = eof_in_bytes / sizeof(T);
  size_t last_word = eof_in_words - is_nonzero_length;
  // Set the last word in the buffer to zero before writing
  // the data to it if we added padding. If we didn't add padding,
  // multiply by 1 which ends up writing back the current contents of that word
  // resulting in a NOP.
  buffer[last_word] *= !needs_padding;
  memcpy(buffer, msg, length.unsafe_get());
  return WordOffset<T>::FromByteOffset(ByteOffset::Unbounded(length.unsafe_get() + padding));
}

// Bitfield definitions copy-pasted from
// https://fuchsia.googlesource.com/fuchsia/+/c81451cd683e/sdk/lib/syslog/streams/cpp/fields.h.

template <size_t begin, size_t end>
struct Field final {
  static_assert(begin < sizeof(uint64_t) * 8, "begin is out of bounds");
  static_assert(end < sizeof(uint64_t) * 8, "end is out of bounds");
  static_assert(begin <= end, "begin must not be larger than end");
  static_assert(end - begin + 1 < 64, "must be a part of a word, not a whole word");

  static constexpr uint64_t kMask = (uint64_t(1) << (end - begin + 1)) - 1;

  template <typename T>
  static constexpr uint64_t Make(T value) {
    return static_cast<uint64_t>(value) << begin;
  }

  template <typename U>
  static constexpr U Get(uint64_t word) {
    return static_cast<U>((word >> (begin % 64)) & kMask);
  }

  static constexpr void Set(uint64_t* word, uint64_t value) {
    *word = (*word & ~(kMask << begin)) | (value << begin);
  }
};

// HeaderField structure for a Record
// see https://fuchsia.dev/fuchsia-src/reference/diagnostics/logs/encoding?hl=en#header
struct HeaderFields final {
  using Type = Field<0, 3>;
  using SizeWords = Field<4, 15>;
  using Reserved = Field<16, 55>;
  using Severity = Field<56, 63>;
};

// ArgumentField structure for an Argument
// see https://fuchsia.dev/fuchsia-src/reference/diagnostics/logs/encoding?hl=en#arguments
struct ArgumentFields final {
  using Type = Field<0, 3>;
  using SizeWords = Field<4, 15>;
  using NameRefVal = Field<16, 30>;
  using NameRefMSB = Field<31, 31>;
  using ValueRef = Field<32, 47>;
  using Reserved = Field<32, 63>;
};

using log_word_t = uint64_t;

// Represents a slice of a buffer of type T.
template <typename T>
class DataSlice final {
 public:
  DataSlice(T* ptr, WordOffset<T> slice) : ptr_(ptr), slice_(slice) {}

  T& operator[](WordOffset<T> offset) {
    offset.AssertValid();
    return ptr_[offset];
  }

  const T& operator[](WordOffset<T> offset) const { return ptr_[offset.unsafe_get()]; }

  WordOffset<T> slice() { return slice_; }

  T* data() { return ptr_; }

 private:
  T* ptr_;
  WordOffset<T> slice_;
};

static DataSlice<const char> SliceFromString(const cpp17::string_view& string) {
  return DataSlice<const char>(
      string.data(), WordOffset<const char>::FromByteOffset(ByteOffset::Unbounded(string.size())));
}

template <typename T, size_t size>
static DataSlice<const T> SliceFromArray(const T (&array)[size]) {
  return DataSlice<const T>(array, size);
}

template <size_t size>
static DataSlice<const char> SliceFromArray(const char (&array)[size]) {
  return DataSlice<const char>(
      array, WordOffset<const char>::FromByteOffset(ByteOffset::Unbounded(size - 1)));
}

struct RecordState final {
  RecordState()
      : arg_size(WordOffset<log_word_t>::FromByteOffset(
            ByteOffset::FromBuffer(0, sizeof(fuchsia_syslog_log_buffer_t::data)))),
        current_key_size(ByteOffset::FromBuffer(0, sizeof(fuchsia_syslog_log_buffer_t::data))),
        cursor(WordOffset<log_word_t>::FromByteOffset(
            ByteOffset::FromBuffer(0, sizeof(fuchsia_syslog_log_buffer_t::data)))) {}
  // Header of the record itself
  uint64_t* header;
  FuchsiaLogSeverity log_severity;
  ::fuchsia::diagnostics::Severity severity;
  // arg_size in words
  WordOffset<log_word_t> arg_size;
  zx::unowned_socket socket;
  // key_size in bytes
  ByteOffset current_key_size;
  // Header of the current argument being encoded
  uint64_t* current_header_position = 0;
  uint32_t dropped_count = 0;
  // Current position (in 64-bit words) into the buffer.
  WordOffset<log_word_t> cursor;
  // True if encoding was successful, false otherwise
  bool encode_success = true;
  // True if end was called
  bool ended = false;
  // Message string -- valid if severity is FATAL. For FATAL
  // logs the caller is responsible for ensuring the string
  // is valid for the duration of the call (which our macros
  // will ensure for current users).
  cpp17::optional<cpp17::string_view> msg_string;
  static RecordState* CreatePtr(fuchsia_syslog_log_buffer_t* buffer) {
    return reinterpret_cast<RecordState*>(&buffer->record_state);
  }
};
static_assert(sizeof(RecordState) <= sizeof(fuchsia_syslog_log_buffer_t::record_state),
              "Expected sizeof(RecordState) <= sizeof(LogBuffer::record_state)");
static_assert(std::alignment_of<RecordState>() == sizeof(uint64_t),
              "Expected std::alignment_of<RecordState>() == sizeof(uint64_t)");

// Used for accessing external data buffers provided by clients.
// Used by the Encoder to do in-place encoding of data
class ExternalDataBuffer final {
 public:
  explicit ExternalDataBuffer(fuchsia_syslog_log_buffer_t* buffer)
      : buffer_(&buffer->data[0]), cursor_(RecordState::CreatePtr(buffer)->cursor) {}

  ExternalDataBuffer(log_word_t* data, size_t length, WordOffset<log_word_t>& cursor)
      : buffer_(data), cursor_(cursor) {}
  __WARN_UNUSED_RESULT bool Write(const log_word_t* data, WordOffset<log_word_t> length) {
    if (!cursor_.in_bounds(length)) {
      // TODO (https://fxbug.dev/80984): Add test for this.
      return false;
    }
    for (size_t i = 0; i < length.unsafe_get(); i++) {
      buffer_[(cursor_ + i).unsafe_get()] = data[i];
    }
    cursor_ = cursor_ + length;
    return true;
  }

  __WARN_UNUSED_RESULT bool WritePadded(const void* msg, const ByteOffset& byte_count,
                                        WordOffset<log_word_t>* written) {
    assert(written != nullptr);
    WordOffset<log_word_t> word_count = cursor_.begin().AddPadded(byte_count);
    if (!cursor_.in_bounds(word_count)) {
      // TODO (https://fxbug.dev/80984): Add test for this.
      return false;
    }
    auto retval = WritePaddedInternal(buffer_ + cursor_.unsafe_get(), msg, byte_count);
    cursor_ = cursor_ + retval;
    *written = retval;
    return true;
  }

  template <typename T>
  __WARN_UNUSED_RESULT bool Write(const T& data) {
    static_assert(sizeof(T) >= sizeof(log_word_t), "Expected sizeof(T) >= sizeof(log_word_t)");
    static_assert(alignof(T) >= sizeof(log_word_t), "Expected alignof(T) >= sizeof(log_word_t)");
    return Write(reinterpret_cast<const log_word_t*>(&data),
                 WordOffset<log_word_t>::FromByteOffset(
                     ByteOffset::Unbounded((sizeof(T) / sizeof(log_word_t)) * sizeof(log_word_t))));
  }

  uint64_t* data() { return buffer_ + cursor_.unsafe_get(); }

  DataSlice<log_word_t> GetSlice() { return DataSlice<log_word_t>(buffer_, cursor_); }

 private:
  // Start of buffer
  log_word_t* buffer_ = nullptr;
  // Current location in buffer (in words)
  WordOffset<log_word_t>& cursor_;
};

// Encoder for structured logs
template <typename T>
class Encoder final {
 public:
  explicit Encoder(T& buffer) { buffer_ = &buffer; }

  // Begins the log record.
  void Begin(RecordState& state, zx::time timestamp, ::fuchsia::diagnostics::Severity severity) {
    state.severity = severity;
    state.header = buffer_->data();
    log_word_t empty_header = 0;
    state.encode_success &= buffer_->Write(empty_header);
    state.encode_success &= buffer_->Write(timestamp.get());
  }

  // Flushes a previous argument after it has been fully encoded.
  void FlushPreviousArgument(RecordState& state) { state.arg_size.reset(); }

  // Appends the key portion of an argument to the encode buffer.
  void AppendArgumentKey(RecordState& state, DataSlice<const char> key) {
    FlushPreviousArgument(state);
    auto header_position = buffer_->data();
    log_word_t empty_header = 0;
    state.encode_success &= buffer_->Write(empty_header);
    WordOffset<log_word_t> s_size =
        WordOffset<log_word_t>::FromByteOffset(ByteOffset::Unbounded(0));
    state.encode_success &= buffer_->WritePadded(key.data(), key.slice().ToByteOffset(), &s_size);
    state.arg_size = s_size + 1;  // offset by 1 for the header
    state.current_key_size = key.slice().ToByteOffset();
    state.current_header_position = header_position;
  }

  // Generates an argument header
  uint64_t ComputeArgHeader(RecordState& state, int type, uint64_t value_ref = 0) {
    return ArgumentFields::Type::Make(type) |
           ArgumentFields::SizeWords::Make(state.arg_size.unsafe_get()) |
           ArgumentFields::NameRefVal::Make(state.current_key_size.unsafe_get()) |
           ArgumentFields::NameRefMSB::Make(state.current_key_size.unsafe_get() > 0 ? 1 : 0) |
           ArgumentFields::ValueRef::Make(value_ref) | ArgumentFields::Reserved::Make(0);
  }

  // Append a value to the current argument
  void AppendArgumentValue(RecordState& state, int64_t value) {
    // int64
    int type = 3;
    state.encode_success &= buffer_->Write(value);
    state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  // Append a value to the current argument
  void AppendArgumentValue(RecordState& state, uint64_t value) {
    // uint64
    int type = 4;
    state.encode_success &= buffer_->Write(value);
    state.arg_size = state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  // Append a value to the current argument
  void AppendArgumentValue(RecordState& state, double value) {
    // double
    int type = 5;
    state.encode_success &= buffer_->Write(value);
    state.arg_size = state.arg_size++;
    *state.current_header_position = ComputeArgHeader(state, type);
  }

  // Append a value to the current argument
  void AppendArgumentValue(RecordState& state, DataSlice<const char> string) {
    // string
    int type = 6;
    WordOffset<log_word_t> written =
        WordOffset<log_word_t>::FromByteOffset(ByteOffset::Unbounded(0));
    state.encode_success &=
        buffer_->WritePadded(string.data(), string.slice().ToByteOffset(), &written);
    state.arg_size = state.arg_size + written;
    *state.current_header_position = ComputeArgHeader(
        state, type, string.slice().unsafe_get() > 0 ? (1 << 15) | string.slice().unsafe_get() : 0);
  }

  // Append a value to the current argument
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

// Compiler thinks this is unused even though WriteLogToSocket uses it.
__UNUSED const char kMessageFieldName[] = "message";
const char kPrintfFieldName[] = "printf";
const char kVerbosityFieldName[] = "verbosity";
const char kPidFieldName[] = "pid";
const char kTidFieldName[] = "tid";
const char kDroppedLogsFieldName[] = "dropped_logs";
const char kFileFieldName[] = "file";
const char kLineFieldName[] = "line";

void BeginRecordInternal(fuchsia_syslog_log_buffer_t* buffer, FuchsiaLogSeverity severity,
                         cpp17::optional<cpp17::string_view> file_name, unsigned int line,
                         cpp17::optional<cpp17::string_view> msg,
                         cpp17::optional<cpp17::string_view> condition, bool is_printf,
                         zx::unowned_socket socket, uint32_t dropped_count, zx_koid_t pid,
                         zx_koid_t tid) {
  cpp17::optional<int8_t> raw_severity;
  // Validate that the severity matches the FIDL definition in
  // sdk/fidl/fuchsia.diagnostics/severity.fidl.
  if ((severity % 0x10) || (severity > 0x60) || (severity < 0x10)) {
    raw_severity = severity;
    severity = FUCHSIA_LOG_DEBUG;
  }
  // Initialize the encoder targeting the passed buffer, and begin the record.
  auto time = zx::clock::get_monotonic();
  auto* state = RecordState::CreatePtr(buffer);
  RecordState& record = *state;
  // Invoke the constructor of RecordState to construct a valid RecordState
  // inside the LogBuffer.
  new (state) RecordState;
  state->socket = std::move(socket);
  if (severity == FUCHSIA_LOG_FATAL) {
    state->msg_string = msg;
  }
  state->log_severity = severity;
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.Begin(*state, time, ::fuchsia::diagnostics::Severity(severity));
  if (is_printf) {
    encoder.AppendArgumentKey(record, SliceFromArray(kPrintfFieldName));
    encoder.AppendArgumentValue(record, static_cast<uint64_t>(0));
  }
  // Initialize common PID/TID fields
  encoder.AppendArgumentKey(record, SliceFromArray(kPidFieldName));
  encoder.AppendArgumentValue(record, static_cast<uint64_t>(pid));
  encoder.AppendArgumentKey(record, SliceFromArray(kTidFieldName));
  encoder.AppendArgumentValue(record, static_cast<uint64_t>(tid));
  record.dropped_count = dropped_count;
  // Initialize optional fields if set.
  if (raw_severity) {
    encoder.AppendArgumentKey(record, SliceFromString(kVerbosityFieldName));
    encoder.AppendArgumentValue(record, static_cast<int64_t>(raw_severity.value()));
  }
  if (dropped_count) {
    encoder.AppendArgumentKey(record, SliceFromString(kDroppedLogsFieldName));
    encoder.AppendArgumentValue(record, static_cast<uint64_t>(dropped_count));
  }
  if (msg) {
    encoder.AppendArgumentKey(record, SliceFromString(kMessageFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(*msg));
  }
  if (file_name) {
    encoder.AppendArgumentKey(record, SliceFromString(kFileFieldName));
    encoder.AppendArgumentValue(record, SliceFromString(*file_name));
  }
  encoder.AppendArgumentKey(record, SliceFromString(kLineFieldName));
  encoder.AppendArgumentValue(record, static_cast<uint64_t>(line));
}

void BeginRecord(fuchsia_syslog_log_buffer_t* buffer, FuchsiaLogSeverity severity,
                 cpp17::optional<cpp17::string_view> file_name, unsigned int line,
                 cpp17::optional<cpp17::string_view> message,
                 cpp17::optional<cpp17::string_view> condition, bool is_printf,
                 zx::unowned_socket socket, uint32_t dropped_count, zx_koid_t pid, zx_koid_t tid) {
  BeginRecordInternal(buffer, severity, file_name, line, message, condition, is_printf,
                      std::move(socket), dropped_count, pid, tid);
}

void WriteKeyValue(fuchsia_syslog_log_buffer_t* buffer, cpp17::string_view key,
                   cpp17::string_view value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(
      *state, DataSlice<const char>(key.data(), WordOffset<const char>::FromByteOffset(
                                                    ByteOffset::Unbounded(key.size()))));
  encoder.AppendArgumentValue(
      *state, DataSlice<const char>(value.data(), WordOffset<const char>::FromByteOffset(
                                                      ByteOffset::Unbounded(value.size()))));
}

void WriteKeyValue(fuchsia_syslog_log_buffer_t* buffer, cpp17::string_view key, int64_t value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(
      *state, DataSlice<const char>(key.data(), WordOffset<const char>::FromByteOffset(
                                                    ByteOffset::Unbounded(key.size()))));
  encoder.AppendArgumentValue(*state, value);
}

void WriteKeyValue(fuchsia_syslog_log_buffer_t* buffer, cpp17::string_view key, uint64_t value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(
      *state, DataSlice<const char>(key.data(), WordOffset<const char>::FromByteOffset(
                                                    ByteOffset::Unbounded(key.size()))));
  encoder.AppendArgumentValue(*state, value);
}

void WriteKeyValue(fuchsia_syslog_log_buffer_t* buffer, cpp17::string_view key, double value) {
  auto* state = RecordState::CreatePtr(buffer);
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.AppendArgumentKey(
      *state, DataSlice<const char>(key.data(), WordOffset<const char>::FromByteOffset(
                                                    ByteOffset::Unbounded(key.size()))));
  encoder.AppendArgumentValue(*state, value);
}

void EndRecord(fuchsia_syslog_log_buffer_t* buffer) {
  auto* state = RecordState::CreatePtr(buffer);
  if (state->ended) {
    return;
  }
  state->ended = true;
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  encoder.End(*state);
}

bool FlushRecord(fuchsia_syslog_log_buffer_t* buffer) {
  auto* state = RecordState::CreatePtr(buffer);
  if (!state->encode_success) {
    return false;
  }
  ExternalDataBuffer external_buffer(buffer);
  Encoder<ExternalDataBuffer> encoder(external_buffer);
  auto slice = external_buffer.GetSlice();
  auto status =
      state->socket->write(0, slice.data(), slice.slice().ToByteOffset().unsafe_get(), nullptr);

  return status != ZX_ERR_BAD_STATE && status != ZX_ERR_PEER_CLOSED;
}

}  // namespace fuchsia_syslog

static cpp17::optional<cpp17::string_view> CStringToStringView(const char* str, size_t len) {
  if (!str) {
    return cpp17::nullopt;
  }
  return cpp17::string_view(str, len);
}

__BEGIN_CDECLS
void syslog_begin_record(fuchsia_syslog_log_buffer_t* buffer, FuchsiaLogSeverity severity,
                         const char* file_name, size_t file_name_length, unsigned int line,
                         const char* message, size_t message_length, const char* condition,
                         size_t condition_length, bool is_printf, zx_handle_t socket,
                         uint32_t dropped_count, zx_koid_t pid, zx_koid_t tid) {
  fuchsia_syslog::BeginRecord(buffer, severity, CStringToStringView(file_name, file_name_length),
                              line, CStringToStringView(message, message_length),
                              CStringToStringView(condition, condition_length), is_printf,
                              zx::unowned_socket(socket), dropped_count, pid, tid);
}

// Writes a key/value pair to the buffer.
void syslog_write_key_value_string(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                   size_t key_length, const char* value, size_t value_length) {
  fuchsia_syslog::WriteKeyValue(buffer, CStringToStringView(key, key_length).value(),
                                CStringToStringView(value, value_length).value());
}

// Writes a key/value pair to the buffer.
void syslog_write_key_value_int64(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                  size_t key_length, int64_t value) {
  fuchsia_syslog::WriteKeyValue(buffer, CStringToStringView(key, key_length).value(), value);
}

// Writes a key/value pair to the buffer.
void syslog_write_key_value_uint64(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                   size_t key_length, uint64_t value) {
  fuchsia_syslog::WriteKeyValue(buffer, CStringToStringView(key, key_length).value(), value);
}

// Writes a key/value pair to the buffer.
void syslog_write_key_value_double(fuchsia_syslog_log_buffer_t* buffer, const char* key,
                                   size_t key_length, double value) {
  fuchsia_syslog::WriteKeyValue(buffer, CStringToStringView(key, key_length).value(), value);
}

// Finalizes the record. It is necessary to call this method
// before flush, and the buffer is considered "read-only"
// after calling this until BeginRecord is called
// again on the buffer to re-initialize it.
void syslog_end_record(fuchsia_syslog_log_buffer_t* buffer) { fuchsia_syslog::EndRecord(buffer); }

// Writes the LogBuffer to the socket.
bool syslog_flush_record(fuchsia_syslog_log_buffer_t* buffer) {
  fuchsia_syslog::EndRecord(buffer);
  return fuchsia_syslog::FlushRecord(buffer);
}
__END_CDECLS
