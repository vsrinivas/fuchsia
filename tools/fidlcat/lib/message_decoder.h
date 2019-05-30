// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_MESSAGE_DECODER_H_
#define TOOLS_FIDLCAT_LIB_MESSAGE_DECODER_H_

#include <src/lib/fxl/logging.h>

#include <memory>
#include <ostream>
#include <string_view>
#include <vector>

#include "lib/fidl/cpp/message.h"
#include "tools/fidlcat/lib/display_options.h"
#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/memory_helpers.h"

namespace fidlcat {

class Field;
class Object;
class Struct;
class Type;

enum class Direction { kUnknown, kClient, kServer };

// Helper to decode a message (request or response). It generates an Object.
class MessageDecoder {
 public:
  MessageDecoder(const fidl::Message& message, bool output_errors = true);
  MessageDecoder(const MessageDecoder* container, uint64_t num_bytes,
                 uint64_t num_handles);

  const uint8_t* byte_pos() const { return byte_pos_; }

  size_t current_offset() const { return byte_pos_ - start_byte_pos_; }

  bool output_errors() const { return output_errors_; }

  bool HasError() const { return error_count_ > 0; }

  // Adds a secondary object. That is data which can't be inlined within an
  // object and which is decoded later.
  void AddSecondaryObject(Field* secondary_object) {
    secondary_objects_.push_back(secondary_object);
  }

  // Used by numeric types to retrieve a numeric value. If there is not enough
  // data, returns false and value is not modified.
  template <typename T>
  bool GetValueAt(uint64_t offset, T* value);

  // Gets the address of some data of |size| at |offset|. If there is not enough
  // data, returns null.
  const uint8_t* GetAddress(uint64_t offset, uint64_t size) {
    if (byte_pos_ + offset + size > end_byte_pos_) {
      if (output_errors_) {
        FXL_LOG(ERROR) << "not enough data to decode (needs " << size
                       << " at offset "
                       << ((byte_pos_ - start_byte_pos_) + offset)
                       << ", remains " << (end_byte_pos_ - byte_pos_) << ")";
      }
      ++error_count_;
      return nullptr;
    }
    return byte_pos_ + offset;
  }

  // Sets the offset to the next object offset. The current object may or may
  // not have been decoded. The offset of the next object is the current
  // object's offset + the current object's size. The new offset is 8 byte
  // aligned.
  void GotoNextObjectOffset(uint64_t size) {
    byte_pos_ += size;
    size_t offset = byte_pos_ - start_byte_pos_;
    byte_pos_ += (8 - (offset & 7)) & 7;
    if (byte_pos_ > end_byte_pos_) {
      if (output_errors_) {
        FXL_LOG(ERROR) << "not enough data at the end of object";
      }
      ++error_count_;
    }
  }

  // Skips the handles we just decoded (used by envelopes).
  void SkipHandles(uint64_t size) {
    handle_pos_ += size;
    if (handle_pos_ > end_handle_pos_) {
      if (output_errors_) {
        FXL_LOG(ERROR) << "not enough handles";
      }
      ++error_count_;
    }
  }

  // Consumes a handle. Returns FIDL_HANDLE_ABSENT if there is no handle
  // available.
  zx_handle_t GetNextHandle() {
    if (handle_pos_ == end_handle_pos_) {
      if (output_errors_) {
        FXL_LOG(ERROR) << "not enough handles";
      }
      ++error_count_;
      return FIDL_HANDLE_ABSENT;
    }
    return *handle_pos_++;
  }

  // Decodes a whole message (request or response) and return an Object.
  std::unique_ptr<Object> DecodeMessage(const Struct& message_format);

  // Decodes a field. Used by envelopes.
  std::unique_ptr<Field> DecodeField(std::string_view name, const Type* type);

 private:
  // The start of the message.
  const uint8_t* const start_byte_pos_;
  const zx_handle_t* const start_handle_pos_;

  // The end of the message.
  const uint8_t* const end_byte_pos_;
  const zx_handle_t* const end_handle_pos_;

  // The current decoding position in the message.
  const uint8_t* byte_pos_;
  const zx_handle_t* handle_pos_;

  // All the values which are not defined within the object they belong to.
  // It is the case, for example, of string, nullable structs, ...
  std::vector<Field*> secondary_objects_;

  // True if we display the errors we find.
  bool output_errors_;

  // Errors found during the message decoding.
  int error_count_ = 0;
};

// Used by numeric types to retrieve a numeric value. If there is not enough
// data, returns false and value is not modified.
template <typename T>
bool MessageDecoder::GetValueAt(uint64_t offset, T* value) {
  if (byte_pos_ + offset + sizeof(T) > end_byte_pos_) {
    if (output_errors_) {
      FXL_LOG(ERROR) << "not enough data to decode (needs " << sizeof(T)
                     << " at offset "
                     << ((byte_pos_ - start_byte_pos_) + offset) << ", remains "
                     << (end_byte_pos_ - byte_pos_) << ")";
    }
    ++error_count_;
    return false;
  }
  *value = internal::MemoryFrom<T>(byte_pos_ + offset);
  return true;
}

void DecodeMessage(
    LibraryLoader* loader,
    std::map<std::tuple<zx_handle_t, uint64_t>, Direction>* handle_directions,
    const DisplayOptions& options, uint64_t process_koid, zx_handle_t handle,
    const fidl::Message& message, bool read, std::ostream& os);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_MESSAGE_DECODER_H_
