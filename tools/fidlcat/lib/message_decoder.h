// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_MESSAGE_DECODER_H_
#define TOOLS_FIDLCAT_LIB_MESSAGE_DECODER_H_

#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <src/lib/fxl/logging.h>

#include "lib/fidl/cpp/message.h"
#include "tools/fidlcat/lib/display_options.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/memory_helpers.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class Field;
class Object;
class Struct;
class Type;

enum class Direction { kUnknown, kClient, kServer };

constexpr int kTabSize = 2;

extern const Colors WithoutColors;
extern const Colors WithColors;

enum class SyscallFidlType {
  kOutputMessage,  // A message (request or response which is written).
  kInputMessage,   // A message (request or response which is read).
  kOutputRequest,  // A request which is written (case of zx_channel_call).
  kInputResponse   // A response which is read (case of zx_channel_call).
};

// Class which is able to decode all the messages received/sent.
class MessageDecoderDispatcher {
 public:
  MessageDecoderDispatcher(LibraryLoader* loader, const DisplayOptions& display_options)
      : loader_(loader),
        display_options_(display_options),
        colors_(display_options.needs_colors ? WithColors : WithoutColors) {}

  LibraryLoader* loader() const { return loader_; }
  const DisplayOptions& display_options() const { return display_options_; }
  const Colors& colors() const { return colors_; }
  bool with_process_info() const { return display_options_.with_process_info; }
  std::map<std::tuple<zx_handle_t, uint64_t>, Direction>& handle_directions() {
    return handle_directions_;
  }

  void AddLaunchedProcess(uint64_t process_koid) { launched_processes_.insert(process_koid); }

  bool IsLaunchedProcess(uint64_t process_koid) {
    return launched_processes_.find(process_koid) != launched_processes_.end();
  }

  bool DecodeMessage(uint64_t process_koid, zx_handle_t handle, const uint8_t* bytes,
                     uint32_t num_bytes, const zx_handle_info_t* handles, uint32_t num_handles,
                     SyscallFidlType type, std::ostream& os, std::string_view line_header = "",
                     int tabs = 0);

 private:
  LibraryLoader* const loader_;
  const DisplayOptions& display_options_;
  const Colors& colors_;
  std::unordered_set<uint64_t> launched_processes_;
  std::map<std::tuple<zx_handle_t, uint64_t>, Direction> handle_directions_;
};

// Helper to decode a message (request or response). It generates an Object.
class MessageDecoder {
 public:
  MessageDecoder(const uint8_t* bytes, uint32_t num_bytes, const zx_handle_info_t* handles,
                 uint32_t num_handles, bool output_errors = false);
  MessageDecoder(const MessageDecoder* container, uint64_t offset, uint64_t num_bytes_remaining,
                 uint64_t num_handles_remaining);

  const zx_handle_info_t* handle_pos() const { return handle_pos_; }

  uint64_t next_object_offset() const { return next_object_offset_; }

  bool output_errors() const { return output_errors_; }

  bool HasError() const { return error_count_ > 0; }

  // Used by numeric types to retrieve a numeric value. If there is not enough
  // data, returns false and value is not modified.
  template <typename T>
  bool GetValueAt(uint64_t offset, T* value);

  // Gets the address of some data of |size| at |offset|. If there is not enough
  // data, returns null.
  const uint8_t* GetAddress(uint64_t offset, uint64_t size) {
    if (offset + size > num_bytes_) {
      if (output_errors_ && (offset <= num_bytes_)) {
        FXL_LOG(ERROR) << "not enough data to decode (needs " << size << " at offset " << offset
                       << ", remains " << (num_bytes_ - offset) << ")";
      }
      ++error_count_;
      return nullptr;
    }
    return start_byte_pos_ + offset;
  }

  // Sets the next object offset. The current object (which is at the previous value of next object
  // offset) is not decoded yet. It will be decoded just after this call.
  // The new offset is 8 byte aligned.
  void SkipObject(uint64_t size) {
    uint64_t new_offset = (next_object_offset_ + size + 7) & ~7;
    if (new_offset > num_bytes_) {
      if (output_errors_ && (next_object_offset_ <= num_bytes_)) {
        FXL_LOG(ERROR) << "not enough data to decode (needs " << size << " at offset "
                       << next_object_offset_ << ", remains " << (num_bytes_ - next_object_offset_);
      }
      ++error_count_;
    }
    next_object_offset_ = new_offset;
  }

  // Consumes a handle. Returns FIDL_HANDLE_ABSENT if there is no handle
  // available.
  zx_handle_info_t GetNextHandle() {
    if (handle_pos_ == end_handle_pos_) {
      if (output_errors_) {
        FXL_LOG(ERROR) << "not enough handles";
      }
      ++error_count_;
      zx_handle_info_t result;
      result.handle = FIDL_HANDLE_ABSENT;
      result.type = ZX_OBJ_TYPE_NONE;
      result.rights = 0;
      return result;
    }
    return *handle_pos_++;
  }

  // Decodes a whole message (request or response) and return an Object.
  std::unique_ptr<Object> DecodeMessage(const Struct& message_format);

  // Decodes a field. Used by envelopes.
  std::unique_ptr<Field> DecodeField(std::string_view name, const Type* type);

 private:
  // The size of the message bytes.
  uint32_t num_bytes_;

  // The start of the message.
  const uint8_t* const start_byte_pos_;

  // The end of the message.
  const zx_handle_info_t* const end_handle_pos_;

  // The current handle decoding position in the message.
  const zx_handle_info_t* handle_pos_;

  // Location of the next out of line object.
  uint64_t next_object_offset_ = 0;

  // True if we display the errors we find.
  bool output_errors_;

  // Errors found during the message decoding.
  int error_count_ = 0;
};

// Used by numeric types to retrieve a numeric value. If there is not enough
// data, returns false and value is not modified.
template <typename T>
bool MessageDecoder::GetValueAt(uint64_t offset, T* value) {
  if (offset + sizeof(T) > num_bytes_) {
    if (output_errors_ && (offset <= num_bytes_)) {
      FXL_LOG(ERROR) << "not enough data to decode (needs " << sizeof(T) << " at offset " << offset
                     << ", remains " << (num_bytes_ - offset) << ")";
    }
    ++error_count_;
    return false;
  }
  *value = internal::MemoryFrom<T>(start_byte_pos_ + offset);
  return true;
}

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_MESSAGE_DECODER_H_
