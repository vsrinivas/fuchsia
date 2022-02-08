// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_MESSAGE_H_
#define LIB_FIDL_CPP_MESSAGE_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/txn_header.h>
#include <zircon/fidl.h>

#include <vector>

namespace fidl {

class HLCPPIncomingMessage;
class HLCPPOutgoingMessage;

// An incoming FIDL transactional message body.
//
// A FIDL transactional message body has two parts: the |bytes| and the |handles|. The |bytes|
// represent only the transactional message's body, and may be a view into a larger buffer that
// is prepended by the transactional message's header bytes as well.
//
// A HLCPPIncomingBody object does not own the storage for the message parts.
class HLCPPIncomingBody {
  // If an |HLCPPIncomingBody| is held by an |HLCPPIncomingMessage| (or, when used as a response to
  // a Call, by an |HLCPPOutgoingMessage|), it is important that the |BytePart|s of the two
  // containers are kept in sync: the |BytePart| of the |HLCPPIncomingMessage| should always contain
  // a pointer 16 bytes before the pointer contained by the |HLCPPIncomingBody| it owns, and the
  // |actual()| size of the former should be 16 bytes larger than the latter. By declaring these two
  // potential owners friends, we allow them resize/modify this inner container whenever the outer
  // one changes, while preventing users of |HLCPPIncomingBody| in the standalone case from directly
  // modifying its innards.
  friend class HLCPPIncomingMessage;
  friend class HLCPPOutgoingMessage;

 public:
  // Creates a message body without any storage.
  HLCPPIncomingBody();

  HLCPPIncomingBody(BytePart bytes, HandleInfoPart handles);

  ~HLCPPIncomingBody();

  HLCPPIncomingBody(const HLCPPIncomingBody& other) = delete;
  HLCPPIncomingBody& operator=(const HLCPPIncomingBody& other) = delete;

  HLCPPIncomingBody(HLCPPIncomingBody&& other);
  HLCPPIncomingBody& operator=(HLCPPIncomingBody&& other);

  // The storage for the bytes of the message body.
  const BytePart& bytes() const { return bytes_; }

  // The storage for the handles of the message body.
  //
  // When the message is encoded, the handle values are stored in this part of
  // the message. When the message is decoded, this part of the message is
  // empty and the handle values are stored in the bytes().
  HandleInfoPart& handles() { return handles_; }
  const HandleInfoPart& handles() const { return handles_; }

  // The message body bytes interpreted as the given type.
  template <typename T>
  T* GetBytesAs() const {
    return reinterpret_cast<T*>(bytes_.data());
  }

  // Decodes the transactional message body in-place.
  //
  // |metadata| describes features/revision information about the wire format.
  zx_status_t Decode(const internal::WireFormatMetadata& metadata, const fidl_type_t* type,
                     const char** error_msg_out);

  // Stop tracking the handles in stored in handles(), without closing them.
  //
  // Typically, these handles will be extracted during decode or the
  // message's destructor, so this function will be unnecessary. However,
  // for clients of ulib/fidl which decode message manually, this function
  // is necessary to prevent extracted handles from being closed.
  void ClearHandlesUnsafe();

 private:
  BytePart bytes_;
  HandleInfoPart handles_;

  // Because |HLCPPIncomingBody| does not own its storage, these methods are only accessible to
  // friend classes that are using |HLCPPIncomingBody| as a view.
  void set_bytes(BytePart bytes) { bytes_ = static_cast<BytePart&&>(bytes); }
  void resize_bytes(uint32_t num_bytes) { bytes_.set_actual(num_bytes); }

  // Transforms the transactional message body in-place.
  //
  // |metadata| describes features/revision information about the wire format.
  zx_status_t Transform(const internal::WireFormatMetadata& metadata, const fidl_type_t* type,
                        uint32_t* new_num_bytes, const char** error_msg_out);
};

// An incoming FIDL transactional message.
//
// A FIDL transactional message has two parts: the |bytes| and the |handles|. The |bytes| are
// divided into a header (of type fidl_message_header_t) and a body, which follows the header.
//
// A HLCPPIncomingMessage object does not own the storage for the message parts.
class HLCPPIncomingMessage {
 public:
  // Creates a message without any storage.
  HLCPPIncomingMessage();

  // Creates a message whose storage is backed by |bytes| and |handles|.
  //
  // The constructed |Message| object does not take ownership of the given
  // storage, although does take ownership of zircon handles contained withing
  // handles.
  HLCPPIncomingMessage(BytePart bytes, HandleInfoPart handles);

  HLCPPIncomingMessage(const HLCPPIncomingMessage& other) = delete;
  HLCPPIncomingMessage& operator=(const HLCPPIncomingMessage& other) = delete;

  HLCPPIncomingMessage(HLCPPIncomingMessage&& other);
  HLCPPIncomingMessage& operator=(HLCPPIncomingMessage&& other);

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

  // Whether this message is in a supported version of the wire format.
  bool is_supported_version() const { return fidl_validate_txn_header(&header()) == ZX_OK; }

  // The message body that follows the header.
  const HLCPPIncomingBody& body_view() const { return body_view_; }
  HLCPPIncomingBody& body_view() { return body_view_; }

  // Is this a message containing only a header?
  bool has_only_header() const { return bytes_.actual() == sizeof(fidl_message_header_t); }

  // The message payload that follows the header interpreted as the given type.
  template <typename T>
  T* GetBodyViewAs() const {
    return reinterpret_cast<T*>(body_view_.bytes().data());
  }

  // The storage for the bytes of the transactional message.
  const BytePart& bytes() const { return bytes_; }

  // The message bytes interpreted as the given type.
  template <typename T>
  T* GetBytesAs() const {
    return reinterpret_cast<T*>(bytes_.data());
  }

  void set_bytes(BytePart bytes) {
    bytes_ = static_cast<BytePart&&>(bytes);
    body_view_.set_bytes(BytePart(bytes_, sizeof(fidl_message_header_t)));
  }
  void resize_bytes(uint32_t num_bytes) {
    bytes_.set_actual(num_bytes);
    body_view_.resize_bytes(num_bytes - sizeof(fidl_message_header_t));
  }

  // The storage for the handles of the transactional message.
  //
  // When the message is encoded, the handle values are stored in this part of the message. When the
  // message is decoded, this part of the message is empty and the handle values are stored in the
  // |bytes()|.
  HandleInfoPart& handles() { return body_view_.handles(); }
  const HandleInfoPart& handles() const { return body_view_.handles(); }

  // Decodes the transactional message in-place.
  //
  // The transactional message must previously have been in an encoded state, for example, either by
  // being read from a zx_channel_t or having been encoded using the |Encode| method.
  zx_status_t Decode(const fidl_type_t* type, const char** error_msg_out);

  // Read a transactional message from the given channel.
  //
  // The bytes read from the channel are stored in |bytes()| and the handles read from the channel
  // are stored in |handles()|. Existing data in these buffers is overwritten.
  zx_status_t Read(zx_handle_t channel, uint32_t flags);

  // Stop tracking the handles in stored in handles(), without closing them.
  //
  // Typically, these handles will be extracted during decode or the
  // message's destructor, so this function will be unnecessary. However,
  // for clients of ulib/fidl which decode message manually, this function
  // is necessary to prevent extracted handles from being closed.
  void ClearHandlesUnsafe();

 private:
  BytePart bytes_;
  HLCPPIncomingBody body_view_;

  // Transforms the transactional message in-place.
  //
  // |metadata| describes features/revision information about the wire format.
  zx_status_t Transform(const internal::WireFormatMetadata& metadata, const fidl_type_t* type,
                        const char** error_msg_out);
};

// An outgoing FIDL transactional message body.
//
// A FIDL transactional message body has two parts: the |bytes| and the |handles|. The |bytes|
// represent only the transactional message's body, and may be a view into a larger buffer that
// is prepended by the transactional message's header bytes as well.
//
// A HLCPPOutgoingBody object does not own the storage for the message parts.
class HLCPPOutgoingBody {
  // If an |HLCPPOutgoingBody| is held by an |HLCPPOutgoingMessage| it is important that the
  // |BytePart|s of the two containers are kept in sync: the |BytePart| of the
  // |HLCPPOutgoingMessage| should always contain a pointer 16 bytes before the pointer contained by
  // the |HLCPPOutgoingBody| it owns, and the |actual()| size of the former should be 16 bytes
  // larger than the latter. By declaring these two potential owners friends, we allow them
  // resize/modify this inner container whenever the outer one changes, while preventing users of
  // |HLCPPOutgoingBody| in the standalone case from directly modifying its innards.
  friend class HLCPPOutgoingMessage;

 public:
  // Creates a message body without any storage.
  HLCPPOutgoingBody();

  HLCPPOutgoingBody(BytePart bytes, HandleDispositionPart handles);

  ~HLCPPOutgoingBody();

  HLCPPOutgoingBody(const HLCPPOutgoingBody& other) = delete;
  HLCPPOutgoingBody& operator=(const HLCPPOutgoingBody& other) = delete;

  HLCPPOutgoingBody(HLCPPOutgoingBody&& other);
  HLCPPOutgoingBody& operator=(HLCPPOutgoingBody&& other);

  // The storage for the bytes of the message body.
  const BytePart& bytes() const { return bytes_; }

  // The storage for the handles of the message body.
  //
  // When the message is encoded, the handle values are stored in this part of
  // the message. When the message is decoded, this part of the message is
  // empty and the handle values are stored in the bytes().
  HandleDispositionPart& handles() { return handles_; }
  const HandleDispositionPart& handles() const { return handles_; }

  // The message body bytes interpreted as the given type.
  template <typename T>
  T* GetBytesAs() const {
    return reinterpret_cast<T*>(bytes_.data());
  }

  // Encodes the message body in-place.
  //
  // The message must previously have been in a decoded state, for example,
  // either by being built in a decoded state using a |Builder| or having been
  // decoded using the |Decode| method.
  zx_status_t Encode(const fidl_type_t* type, const char** error_msg_out);

  // Validates the message body in-place.
  //
  // The message must already be in an encoded state, for example, either by
  // being read from a zx_channel_t or having been created in that state.
  //
  // Does not modify the message.
  zx_status_t Validate(const internal::WireFormatVersion& version, const fidl_type_t* type,
                       const char** error_msg_out) const;

  // Stop tracking the handles in stored in handles(), without closing them.
  //
  // Typically, these handles will be extracted during decode or the
  // message's destructor, so this function will be unnecessary. However,
  // for clients of ulib/fidl which decode message manually, this function
  // is necessary to prevent extracted handles from being closed.
  void ClearHandlesUnsafe();

 private:
  BytePart bytes_;
  HandleDispositionPart handles_;

  void set_bytes(BytePart bytes) { bytes_ = static_cast<BytePart&&>(bytes); }
  void resize_bytes(uint32_t num_bytes) { bytes_.set_actual(num_bytes); }

  // Transforms the message body in-place.
  //
  // |metadata| describes features/revision information about the wire format.
  zx_status_t Transform(const internal::WireFormatMetadata& metadata, const fidl_type_t* type,
                        uint32_t* new_num_bytes, const char** error_msg_out);
};

// An outgoing FIDL transactional message.
//
// A FIDL transactional message has two parts: the |bytes| and the |handles|. The |bytes| are
// divided into a header (of type fidl_message_header_t) and a body, which follows the header.
//
// A HLCPPOutgoingMessage object does not own the storage for the message parts.
class HLCPPOutgoingMessage {
 public:
  // Creates a message without any storage.
  HLCPPOutgoingMessage();

  // Creates a message whose storage is backed by |bytes| and |handles|.
  //
  // The constructed |Message| object does not take ownership of the given
  // storage, although does take ownership of zircon handles contained withing
  // handles.
  HLCPPOutgoingMessage(BytePart bytes, HandleDispositionPart handles);

  HLCPPOutgoingMessage(const HLCPPOutgoingMessage& other) = delete;
  HLCPPOutgoingMessage& operator=(const HLCPPOutgoingMessage& other) = delete;

  HLCPPOutgoingMessage(HLCPPOutgoingMessage&& other);
  HLCPPOutgoingMessage& operator=(HLCPPOutgoingMessage&& other);

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

  // The message body that follows the header.
  const HLCPPOutgoingBody& body_view() const { return body_view_; }

  // Is this a message containing only a header?
  bool has_only_header() const { return bytes_.actual() == sizeof(fidl_message_header_t); }

  // The storage for the bytes of the transactional message.
  const BytePart& bytes() const { return bytes_; }

  void set_bytes(BytePart bytes) {
    bytes_ = static_cast<BytePart&&>(bytes);
    body_view_.set_bytes(BytePart(bytes_, sizeof(fidl_message_header_t)));
  }
  void resize_bytes(uint32_t num_bytes) {
    bytes_.set_actual(num_bytes);
    body_view_.resize_bytes(num_bytes - sizeof(fidl_message_header_t));
  }

  // The storage for the handles of the message.
  //
  // When the message is encoded, the handle values are stored in this part of
  // the message. When the message is decoded, this part of the message is
  // empty and the handle values are stored in the bytes().
  HandleDispositionPart& handles() { return body_view_.handles(); }
  const HandleDispositionPart& handles() const { return body_view_.handles(); }

  // Encodes the transactional message body in-place.
  //
  // The message must previously have been in a decoded state, for example,
  // either by being built in a decoded state using a |Builder| or having been
  // decoded using the |Decode| method.
  zx_status_t Encode(const fidl_type_t* type, const char** error_msg_out);

  // Validates the transactional message body in-place.
  //
  // The message must already be in an encoded state, for example, either by
  // being read from a zx_channel_t or having been created in that state.
  //
  // Does not modify the message.
  zx_status_t Validate(const fidl_type_t* type, const char** error_msg_out) const;

  // Writes a transactional message to the given channel.
  //
  // The bytes stored in bytes() are written to the channel and the handles
  // stored in handles() are written to the channel.
  //
  // If this method returns ZX_OK, handles() will be empty because they were
  // consumed by this operation.
  zx_status_t Write(zx_handle_t channel, uint32_t flags);

  // Issues a synchronous send and receive transaction on the given channel.
  //
  // The bytes stored in bytes() are written to the channel and the handles
  // stored in handles() are written to the channel. The bytes read from the
  // channel are stored in response->bytes() and the handles read from the
  // channel are stored in response->handles().
  //
  // If this method returns ZX_OK, handles() will be empty because they were
  // consumed by this operation.
  zx_status_t Call(zx_handle_t channel, uint32_t flags, zx_time_t deadline,
                   HLCPPIncomingMessage* response);

  // Stop tracking the handles in stored in handles(), without closing them.
  //
  // Typically, these handles will be extracted during decode or the
  // message's destructor, so this function will be unnecessary. However,
  // for clients of ulib/fidl which decode message manually, this function
  // is necessary to prevent extracted handles from being closed.
  void ClearHandlesUnsafe();

 private:
  BytePart bytes_;
  HLCPPOutgoingBody body_view_;

  // Transforms the transactional message in-place.
  //
  // |metadata| describes features/revision information about the wire format.
  zx_status_t Transform(const internal::WireFormatMetadata& metadata, const fidl_type_t* type,
                        const char** error_msg_out);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_MESSAGE_H_
