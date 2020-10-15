// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_
#define LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/internal_callable_traits.h>
#include <lib/fidl/llcpp/traits.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>
#endif

namespace fidl {

namespace internal {

// When |NumHandles| is zero, |handle_storage| is always NULL.
// This way we avoid declaring a C array with zero number of elements.
template <uint32_t MaxNumHandles, typename Enabled = void>
class EncodedMessageHandleHolder;

template <uint32_t MaxNumHandles>
class EncodedMessageHandleHolder<MaxNumHandles, std::enable_if_t<(MaxNumHandles > 0)>> {
 protected:
  constexpr static uint32_t kResolvedMaxHandles =
      MaxNumHandles > ZX_CHANNEL_MAX_MSG_HANDLES ? ZX_CHANNEL_MAX_MSG_HANDLES : MaxNumHandles;

  zx_handle_t* handle_storage() { return &handle_storage_[0]; }

 private:
  zx_handle_t handle_storage_[kResolvedMaxHandles];
};

template <uint32_t MaxNumHandles>
class EncodedMessageHandleHolder<MaxNumHandles, std::enable_if_t<(MaxNumHandles == 0)>> {
 protected:
  constexpr static uint32_t kResolvedMaxHandles = 0;

  zx_handle_t* handle_storage() { return nullptr; }
};

}  // namespace internal

class RawMessage {
 public:
  RawMessage(BytePart bytes, HandlePart handles)
      : bytes_(std::move(bytes)), handles_(std::move(handles)) {}
  explicit RawMessage(HandlePart handles) : handles_(std::move(handles)) {}

  BytePart& bytes() { return bytes_; }
  const BytePart& bytes() const { return bytes_; }

  HandlePart& handles() { return handles_; }
  const HandlePart& handles() const { return handles_; }

 private:
  BytePart bytes_;
  HandlePart handles_;
};

// Holds an encoded FIDL message, that is, a byte array plus a handle table.
//
// The bytes part points to an external caller-managed buffer, while the handles part
// is owned by this class. Any handles will be closed upon destruction.
// This class is aware of the upper bound on the number of handles
// in a message, such that its size can be adjusted to fit the demands
// of a specific FIDL type.
//
// Because this class does not own the underlying message buffer, the caller
// must make sure the lifetime of this class does not extend over that of the buffer.
//
// TODO(fxbug.dev/8093): Right now we assume EncodedMessage is always used in a |kReceiving|
// context, which over-allocates bytes and handles for flexible messages. To be more frugal with
// allocation, we should plumb the context through EncodedMessage.
template <typename FidlType>
class EncodedMessage final
    : public internal::EncodedMessageHandleHolder<
          internal::ClampedHandleCount<FidlType, MessageDirection::kReceiving>()> {
  static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
  static_assert(FidlType::PrimarySize > 0, "Positive message size");

  using Super = internal::EncodedMessageHandleHolder<
      internal::ClampedHandleCount<FidlType, MessageDirection::kReceiving>()>;

 public:
  // The maximum number of handles allowed in a message of this type, given the constraints
  // of a zircon channel packet.
  constexpr static uint32_t kResolvedMaxHandles = Super::kResolvedMaxHandles;

  // Instantiates an empty buffer with no bytes or handles.
  EncodedMessage() : message_(HandlePart(Super::handle_storage(), kResolvedMaxHandles)) {}

  // Construct an |EncodedMessage| borrowing the bytes and taking ownership of handles in |msg|.
  // The number of handles in |msg| must not exceed |kResolvedMaxHandles|.
  explicit EncodedMessage(fidl_incoming_msg_t* msg)
      : message_(BytePart(static_cast<uint8_t*>(msg->bytes), msg->num_bytes, msg->num_bytes),
                 HandlePart(Super::handle_storage(), kResolvedMaxHandles)) {
    ZX_ASSERT(msg->num_handles <= kResolvedMaxHandles);
    if (kResolvedMaxHandles > 0) {
      memcpy(handles().data(), msg->handles, sizeof(zx_handle_t) * msg->num_handles);
      for (uint32_t i = 0; i < msg->num_handles; i++) {
        msg->handles[i] = ZX_HANDLE_INVALID;
      }
      handles().set_actual(msg->num_handles);
    } else {
      handles().set_actual(0);
    }
  }

  EncodedMessage(EncodedMessage&& other) noexcept
      : message_(HandlePart(Super::handle_storage(), kResolvedMaxHandles)) {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
  }

  EncodedMessage& operator=(EncodedMessage&& other) noexcept {
    if (this != &other) {
      MoveImpl(std::move(other));
    }
    return *this;
  }

  EncodedMessage(const EncodedMessage& other) = delete;

  EncodedMessage& operator=(const EncodedMessage& other) = delete;

  // Instantiates an EncodedMessage which points to a buffer region with caller-managed memory.
  // It does not take ownership of that buffer region.
  // Also initializes an empty handles part.
  explicit EncodedMessage(BytePart bytes)
      : message_(std::move(bytes), HandlePart(Super::handle_storage(), kResolvedMaxHandles)) {}

  ~EncodedMessage() { CloseHandles(); }

  // Takes ownership of the contents of the message.
  // The bytes and handle parts will become empty, while the existing bytes part is returned.
  // The caller is responsible for having transferred the handles elsewhere
  // before calling this method.
  BytePart ReleaseBytesAndHandles() {
    handles().set_actual(0);
    return std::move(bytes());
  }

  const BytePart& bytes() const { return message_.bytes(); }
  BytePart& bytes() { return message_.bytes(); }

  const HandlePart& handles() const { return message_.handles(); }
  HandlePart& handles() { return message_.handles(); }

  // Take ownership of bytes and handles and assemble into a |fidl::Message|.
  Message ToAnyMessage() { return Message(std::move(bytes()), std::move(handles())); }

 private:
  void CloseHandles() {
    if (kResolvedMaxHandles == 0) {
      return;
    }
    if (handles().actual() > 0) {
#ifdef __Fuchsia__
      ZX_DEBUG_ASSERT(handles().actual() <= kResolvedMaxHandles);
      zx_handle_close_many(handles().data(), handles().actual());
#else
      // How did we have handles if not on Fuchsia? Something bad happened...
      assert(false);
#endif
      handles().set_actual(0);
    }
  }

  void MoveImpl(EncodedMessage&& other) noexcept {
    CloseHandles();
    bytes() = std::move(other.bytes());
#ifdef __Fuchsia__
    ZX_DEBUG_ASSERT(other.handles().actual() <= kResolvedMaxHandles);
#endif
    if (kResolvedMaxHandles > 0) {
      // copy handles from |other|
      memcpy(Super::handle_storage(), other.Super::handle_storage(),
             other.handles().actual() * sizeof(zx_handle_t));
    }
    // release handles in |other|
    handles().set_actual(other.handles().actual());
    other.handles().set_actual(0);
  }

  RawMessage message_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_
