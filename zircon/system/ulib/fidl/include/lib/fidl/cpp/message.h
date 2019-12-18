// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_MESSAGE_H_
#define LIB_FIDL_CPP_MESSAGE_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/transformer.h>
#include <lib/fidl/txn_header.h>
#include <zircon/fidl.h>

#include <vector>

namespace fidl {

const fidl_type_t* get_alt_type(const fidl_type_t* type);

// This is a higher level wrapper around fidl_transform that is responsible for
// allocating memory for the transformed bytes, then calls the provided callback
// on the transformed bytes.
//
// This function will avoid calling fidl_transform whenever possible by checking
// the fidl_type_t's contains_union field, and will also stack or heap allocate
// depending on the possible size of the output bytes.
zx_status_t FidlTransformWithCallback(
    fidl_transformation_t transformation, const fidl_type_t* type, const uint8_t* src_bytes,
    uint32_t src_num_bytes, const char** out_error_msg,
    const std::function<zx_status_t(const uint8_t* dst_bytes, uint32_t dst_num_bytes)>& callback);

// A FIDL message.
//
// A FIDL message has two parts: the bytes and the handles. The bytes are
// divided into a header (of type fidl_message_header_t) and a payload, which
// follows the header.
//
// A Message object does not own the storage for the message parts.
class Message {
 public:
  // Creates a message without any storage.
  Message();

  // Creates a message whose storage is backed by |bytes| and |handles|.
  //
  // The constructed |Message| object does not take ownership of the given
  // storage, although does take ownership of zircon handles contained withing
  // handles.
  Message(BytePart bytes, HandlePart handles);

  ~Message();

  Message(const Message& other) = delete;
  Message& operator=(const Message& other) = delete;

  Message(Message&& other);
  Message& operator=(Message&& other);

  // The header at the start of the message.
  const fidl_message_header_t& header() const {
    return *reinterpret_cast<fidl_message_header_t*>(bytes_.data());
  }
  fidl_message_header_t& header() {
    return *reinterpret_cast<fidl_message_header_t*>(bytes_.data());
  }

  // The transaction ID in the message header.
  zx_txid_t txid() const { return header().txid; }
  void set_txid(zx_txid_t txid) { header().txid = txid; }

  // The ordinal in the message header.
  uint64_t ordinal() const { return header().ordinal; }

  bool is_v1_message() const {
    return fidl_should_decode_union_from_xunion(GetBytesAs<fidl_message_header_t>());
  }

  // Whether this message is in a supported version of the wire format.
  bool is_supported_version() const {
    return fidl_validate_txn_header(GetBytesAs<fidl_message_header_t>()) == ZX_OK;
  }

  // The message payload that follows the header.
  BytePart payload() const {
    constexpr uint32_t n = sizeof(fidl_message_header_t);
    return BytePart(bytes_.data() + n, bytes_.capacity() - n, bytes_.actual() - n);
  }

  // The message bytes interpreted as the given type.
  template <typename T>
  T* GetBytesAs() const {
    return reinterpret_cast<T*>(bytes_.data());
  }

  // The message payload that follows the header interpreted as the given type.
  template <typename T>
  T* GetPayloadAs() const {
    return reinterpret_cast<T*>(bytes_.data() + sizeof(fidl_message_header_t));
  }

  // The storage for the bytes of the message.
  BytePart& bytes() { return bytes_; }
  const BytePart& bytes() const { return bytes_; }
  void set_bytes(BytePart bytes) { bytes_ = static_cast<BytePart&&>(bytes); }

  // The storage for the handles of the message.
  //
  // When the message is encoded, the handle values are stored in this part of
  // the message. When the message is decoded, this part of the message is
  // empty and the handle values are stored in the bytes().
  HandlePart& handles() { return handles_; }
  const HandlePart& handles() const { return handles_; }

  // Encodes the message in-place.
  //
  // The message must previously have been in a decoded state, for example,
  // either by being built in a decoded state using a |Builder| or having been
  // decoded using the |Decode| method.
  zx_status_t Encode(const fidl_type_t* type, const char** error_msg_out);

  // Decodes the message in-place.
  //
  // The message must previously have been in an encoded state, for example,
  // either by being read from a zx_channel_t or having been encoded using the
  // |Encode| method.
  zx_status_t Decode(const fidl_type_t* type, const char** error_msg_out);

  // Validates the message in-place.
  //
  // The message must already be in an encoded state, for example, either by
  // being read from a zx_channel_t or having been created in that state.
  //
  // Does not modify the message.
  zx_status_t Validate(const fidl_type_t* type, const char** error_msg_out) const;

  // Read a message from the given channel.
  //
  // The bytes read from the channel are stored in bytes() and the handles
  // read from the channel are stored in handles(). Existing data in these
  // buffers is overwritten.
  zx_status_t Read(zx_handle_t channel, uint32_t flags);

  // Writes a message to the given channel.
  //
  // The bytes stored in bytes() are written to the channel and the handles
  // stored in handles() are written to the channel.
  //
  // If this method returns ZX_OK, handles() will be empty because they were
  // consumed by this operation.
  zx_status_t Write(zx_handle_t channel, uint32_t flags);

  // Writes a message to the given channel, possibly transforming it first.
  //
  // This method is similar to Write, but also takes in a fidl_type_t
  // to transform the message (if it contains a union) to the v1 wire format
  // before sending it. Since FIDL bindings automatically do this, the
  // WriteTransform method is intended primarily for usecases where FIDL messages
  // must be send manually.
  zx_status_t WriteTransformV1(zx_handle_t channel, uint32_t flags, const fidl_type_t* type);

  // Issues a synchronous send and receive transaction on the given channel.
  //
  // The bytes stored in bytes() are written to the channel and the handles
  // stored in handles() are written to the channel. The bytes read from the
  // channel are stored in response->bytes() and the handles read from the
  // channel are stored in response->handles().
  //
  // If this method returns ZX_OK, handles() will be empty because they were
  // consumed by this operation.
  zx_status_t Call(zx_handle_t channel, uint32_t flags, zx_time_t deadline, Message* response);

  // Stop tracking the handles in stored in handles(), without closing them.
  //
  // Typically, these handles will be extracted during decode or the
  // message's destructor, so this function will be unnecessary. However,
  // for clients of ulib/fidl which decode message manually, this function
  // is necessary to prevent extracted handles from being closed.
  void ClearHandlesUnsafe();

 private:
  BytePart bytes_;
  HandlePart handles_;
  std::vector<uint8_t> allocated_buffer;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_MESSAGE_H_
