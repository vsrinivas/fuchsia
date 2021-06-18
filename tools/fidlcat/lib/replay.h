// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_REPLAY_H_
#define TOOLS_FIDLCAT_LIB_REPLAY_H_

#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include "src/lib/fidl_codec/library_loader.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

// A buffer used to store the state of a channel syscall while the bytes and handles are decoded.
class ReplayBuffer {
 public:
  enum class Kind { kRead, kWrite, kCall };

  ReplayBuffer(uint64_t invoked_timestamp, uint64_t process_id, uint64_t thread_id, Kind kind,
               bool etc, zx_handle_t channel)
      : invoked_timestamp_(invoked_timestamp),
        process_id_(process_id),
        thread_id_(thread_id),
        kind_(kind),
        etc_(etc),
        channel_(channel) {}

  uint64_t thread_id() const { return thread_id_; }
  Kind kind() const { return kind_; }
  zx_handle_t channel() const { return channel_; }
  zx_status_t status() const { return status_; }
  const std::vector<uint8_t>& write_bytes() const { return write_bytes_; }
  const std::vector<zx_handle_disposition_t>& write_handles() const { return write_handles_; }
  const std::vector<uint8_t>& read_bytes() const { return read_bytes_; }
  const std::vector<zx_handle_disposition_t>& read_handles() const { return read_handles_; }

  // True if all the data for the syscall has been decoded.
  bool DecodeOk() const {
    return status_set_ && (write_bytes_.size() == write_byte_count_) &&
           (write_handles_.size() == write_handle_count_) &&
           (read_bytes_.size() == read_byte_count_) && (read_handles_.size() == read_handle_count_);
  }

  Syscall* GetSyscall(SyscallDisplayDispatcher* dispatcher) const;

  void SetWrite(uint32_t write_byte_count, uint32_t write_handle_count) {
    write_byte_count_ = write_byte_count;
    write_handle_count_ = write_handle_count;
  }

  void SetRead(uint32_t read_byte_count, uint32_t read_handle_count) {
    read_byte_count_ = read_byte_count;
    read_handle_count_ = read_handle_count;
  }

  void SetStatus(uint64_t output_timestamp, zx_status_t status) {
    output_timestamp_ = output_timestamp;
    status_ = status;
    status_set_ = true;
  }

  void AddWriteBytes(std::istream& stream);
  void AddWriteHandles(std::istream& stream);
  void AddWriteEtcHandle(std::istream& stream);
  void AddReadBytes(std::istream& stream);
  void AddReadHandles(std::istream& stream);

  // When all the data has been decoded, creates the invoke and output events and adds them to the
  // dispatcher.
  void Dispatch(SyscallDisplayDispatcher* dispatcher);

 private:
  uint64_t invoked_timestamp_;
  uint64_t process_id_;
  uint64_t thread_id_;
  const Kind kind_;
  // True is the syscall is one of zx_channel_read_etc, zx_channel_write_etc and
  // zx_channel_call_etc.
  const bool etc_;
  const zx_handle_t channel_;
  uint32_t write_byte_count_ = 0;
  uint32_t write_handle_count_ = 0;
  uint32_t read_byte_count_ = 0;
  uint32_t read_handle_count_ = 0;
  uint64_t output_timestamp_ = 0;
  zx_status_t status_ = ZX_OK;
  bool status_set_ = false;
  std::vector<uint8_t> write_bytes_;
  std::vector<zx_handle_disposition_t> write_handles_;
  std::vector<uint8_t> read_bytes_;
  std::vector<zx_handle_disposition_t> read_handles_;
};

// Class to replay a previously stored session. All the formatting options can be used (for example
// the filtering of message).
class Replay : public EventDecoder {
 public:
  explicit Replay(SyscallDisplayDispatcher* dispatcher) : EventDecoder(dispatcher) {}

  ReplayBuffer* SearchBuffer(uintptr_t instance) {
    auto result = buffers_.find(instance);
    if (result == buffers_.end()) {
      return nullptr;
    }
    return result->second.get();
  }

  // Dumps in text a binary protobuf file which contains a session.
  bool DumpProto(const std::string& proto_file_name);
  bool DumpProto(std::istream& is);

  // Replays a previously save session.
  bool ReplayProto(const std::string& proto_file_name);
  bool ReplayProto(const std::string& file_name, std::istream& is);

  // Decodes traces.
  void DecodeTrace(std::istream& is);
  void DecodeTraceLine(std::istream& is);

 private:
  // Syscalls currently decoded from a trace.
  std::map<uintptr_t, std::unique_ptr<ReplayBuffer>> buffers_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_REPLAY_H_
