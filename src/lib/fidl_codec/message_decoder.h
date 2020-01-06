// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_MESSAGE_DECODER_H_
#define SRC_LIB_FIDL_CODEC_MESSAGE_DECODER_H_

#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "lib/fidl/cpp/message.h"
#include "lib/fidl/txn_header.h"
#include "src/lib/fidl_codec/colors.h"
#include "src/lib/fidl_codec/display_options.h"
#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/memory_helpers.h"
#include "src/lib/fxl/logging.h"

namespace fidl_codec {

class MessageDecoderDispatcher;
class Struct;
class StructValue;
class Type;
class Value;

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

class DecodedMessage {
 public:
  DecodedMessage() = default;

  // Decodes a message and fill all the fields. Returns true if we can display something.
  bool DecodeMessage(MessageDecoderDispatcher* dispatcher, uint64_t process_koid,
                     zx_handle_t handle, const uint8_t* bytes, uint32_t num_bytes,
                     const zx_handle_info_t* handles, uint32_t num_handles, SyscallFidlType type,
                     std::ostream& os, std::string_view line_header = "", int tabs = 0);

  // Displays a decoded message using the fields. Returns true if we have been able to display
  // correctly the message.
  bool Display(const Colors& colors, bool pretty_print, int columns, std::ostream& os,
               std::string_view line_header, int tabs);

 private:
  const fidl_message_header_t* header_ = nullptr;
  const InterfaceMethod* method_ = nullptr;
  std::unique_ptr<StructValue> decoded_request_;
  std::stringstream request_error_stream_;
  bool matched_request_ = false;
  std::unique_ptr<StructValue> decoded_response_;
  std::stringstream response_error_stream_;
  bool matched_response_ = false;
  Direction direction_ = Direction::kUnknown;
  bool is_request_ = false;
  const char* message_direction_ = "";
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

  // Heuristic which computes the direction of a message (outgoing request, incomming response,
  // ...).
  Direction ComputeDirection(uint64_t process_koid, zx_handle_t handle, SyscallFidlType type,
                             const InterfaceMethod* method, bool only_one_valid);

  // Update the direction. Used when the heuristic was wrong.
  void UpdateDirection(uint64_t process_koid, zx_handle_t handle, Direction direction) {
    handle_directions_[std::make_tuple(handle, process_koid)] = direction;
  }

 private:
  LibraryLoader* const loader_;
  const DisplayOptions& display_options_;
  const Colors& colors_;
  std::unordered_set<uint64_t> launched_processes_;
  std::map<std::tuple<zx_handle_t, uint64_t>, Direction> handle_directions_;
};

// Helper to decode a message (request or response). It generates a StructValue.
class MessageDecoder {
 public:
  MessageDecoder(const uint8_t* bytes, uint32_t num_bytes, const zx_handle_info_t* handles,
                 uint32_t num_handles, std::ostream& error_stream);
  MessageDecoder(MessageDecoder* container, uint64_t offset, uint64_t num_bytes_remaining,
                 uint64_t num_handles_remaining);

  uint32_t absolute_offset() const { return absolute_offset_; }

  uint32_t num_bytes() const { return num_bytes_; }

  const zx_handle_info_t* handle_pos() const { return handle_pos_; }

  uint64_t next_object_offset() const { return next_object_offset_; }

  bool unions_are_xunions() const { return unions_are_xunions_; }

  bool HasError() const { return error_count_ > 0; }

  // Add an error.
  std::ostream& AddError() {
    ++error_count_;
    return error_stream_;
  }

  size_t GetRemainingHandles() const { return end_handle_pos_ - handle_pos_; }

  // Used by numeric types to retrieve a numeric value. If there is not enough
  // data, returns false and value is not modified.
  template <typename T>
  bool GetValueAt(uint64_t offset, T* value);

  // Gets the address of some data of |size| at |offset|. If there is not enough
  // data, returns null.
  const uint8_t* GetAddress(uint64_t offset, uint64_t size) {
    if (offset + size > num_bytes_) {
      AddError() << std::hex << (absolute_offset_ + offset) << std::dec
                 << ": Not enough data to decode (needs " << size << ", remains "
                 << (num_bytes_ - offset) << ")\n";
      return nullptr;
    }
    return start_byte_pos_ + offset;
  }

  // Same as GetAddress but copies the fetched data into a new vector. Returns std::nullopt when
  // GetAddress would return null.
  std::optional<std::vector<uint8_t>> CopyAddress(uint64_t offset, uint64_t size) {
    auto got = GetAddress(offset, size);
    if (!got) {
      return std::nullopt;
    }

    return std::vector(got, got + size);
  }

  // Sets the next object offset. The current object (which is at the previous value of next object
  // offset) is not decoded yet. It will be decoded just after this call.
  // The new offset is 8 byte aligned.
  void SkipObject(uint64_t size) {
    uint64_t new_offset = (next_object_offset_ + size + 7) & ~7;
    if (new_offset > num_bytes_) {
      AddError() << std::hex << (absolute_offset_ + next_object_offset_) << std::dec
                 << ": Not enough data to decode (needs " << (new_offset - next_object_offset_)
                 << ", remains " << (num_bytes_ - next_object_offset_) << ")\n";
      new_offset = num_bytes_;
    }
    next_object_offset_ = new_offset;
  }

  // Consumes a handle. Returns FIDL_HANDLE_ABSENT if there is no handle
  // available.
  zx_handle_info_t GetNextHandle() {
    if (handle_pos_ == end_handle_pos_) {
      AddError() << "Not enough handles\n";
      zx_handle_info_t result;
      result.handle = FIDL_HANDLE_ABSENT;
      result.type = ZX_OBJ_TYPE_NONE;
      result.rights = 0;
      return result;
    }
    return *handle_pos_++;
  }

  // Decodes a whole message (request or response) and return a StructValue.
  std::unique_ptr<StructValue> DecodeMessage(const Struct& message_format);

  // Decodes a field. Used by envelopes.
  std::unique_ptr<Value> DecodeValue(const Type* type);

 private:
  // The absolute offset in the main buffer.
  const uint32_t absolute_offset_ = 0;

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

  // True if we must decode unions as xunions.
  const bool unions_are_xunions_;

  // Errors found during the message decoding.
  int error_count_ = 0;

  // Stream for the errors.
  std::ostream& error_stream_;
};

// Used by numeric types to retrieve a numeric value. If there is not enough
// data, returns false and value is not modified.
template <typename T>
bool MessageDecoder::GetValueAt(uint64_t offset, T* value) {
  if (offset + sizeof(T) > num_bytes_) {
    if (offset <= num_bytes_) {
      AddError() << std::hex << (absolute_offset_ + offset) << std::dec
                 << ": Not enough data to decode (needs " << sizeof(T) << ", remains "
                 << (num_bytes_ - offset) << ")\n";
    }
    return false;
  }
  *value = internal::MemoryFrom<T>(start_byte_pos_ + offset);
  return true;
}

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_MESSAGE_DECODER_H_
