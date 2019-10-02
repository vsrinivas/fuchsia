// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERFACE_HANDLE_H_
#define LIB_FIDL_CPP_INTERFACE_HANDLE_H_

#include <lib/zx/channel.h>
#include <zircon/assert.h>

#include <cstddef>
#include <utility>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/interface_request.h"

namespace fidl {
class Builder;
template <typename Interface>
class InterfacePtr;
template <typename Interface>
class SynchronousInterfacePtr;

// The client endpoint of a FIDL channel.
//
// The remote end of the channel expects this end of the channel to speak the
// protocol associated with |Interface|. This type is the dual of
// |InterfaceRequest|.
//
// Unlike an |InterfacePtr|, an |InterfaceHandle| does not have thread affinity
// and can therefore be transferred to another thread or another process. To
// create an |InterfacePtr| to send messages on this channel, call the |Bind()|
// method, either on the |InterfaceHandle| or the |InterfacePtr| object.
//
// See also:
//
//  * |InterfaceRequest|, which is the server analog of an |InterfaceHandle|.
template <typename Interface>
class InterfaceHandle final {
 public:
  // Creates an |InterfaceHandle| whose underlying channel is invalid.
  InterfaceHandle() = default;

  // Creates an |InterfaceHandle| that wraps the given |channel|.
  explicit InterfaceHandle(zx::channel channel) : channel_(std::move(channel)) {}

  InterfaceHandle(const InterfaceHandle& other) = delete;
  InterfaceHandle& operator=(const InterfaceHandle& other) = delete;

  InterfaceHandle(InterfaceHandle&& other) : channel_(std::move(other.channel_)) {}

  InterfaceHandle& operator=(InterfaceHandle&& other) {
    channel_ = std::move(other.channel_);
    return *this;
  }

  // Implicit conversion from nullptr to an |InterfaceHandle| without a valid
  // |channel|.
  InterfaceHandle(std::nullptr_t) {}

  // Implicit conversion from |InterfacePtr| unbinds the channel from the
  // |InterfacePtr|.
  //
  // This requires the caller to provide an rvalue reference, as the caller's
  // InterfacePtr is effectively moved out of.
  //
  // Making this constructor templated ensures that it is not type-instantiated
  // unless it is used, making the InterfacePtr<->InterfaceHandle codependency
  // less fragile.
  //
  // The std::enable_if_t avoids creation of unintended implicit type
  // conversions (especially from anything else that has an "Unbind()"),
  // presumably due to InterfacePtrType being inferred to be something other
  // than InterfacePtr<Interface>.  However, if a caller is trying to use a type
  // that's only incorrect due to not being an rvalue reference, we do permit
  // this constructor to be selected, but then static_assert() with a message
  // suggesting std::move().
  template <typename InterfacePtrType = InterfacePtr<Interface>,
            typename std::enable_if_t<
                std::is_same<InterfacePtr<Interface>,
                             typename std::remove_reference<InterfacePtrType>::type>::value ||
                    std::is_same<SynchronousInterfacePtr<Interface>,
                                 typename std::remove_reference<InterfacePtrType>::type>::value,
                int>
                zero_not_used = 0>
  InterfaceHandle(InterfacePtrType&& ptr) {
    static_assert(std::is_same<InterfacePtr<Interface>&&, decltype(ptr)>::value ||
                      std::is_same<SynchronousInterfacePtr<Interface>&&, decltype(ptr)>::value,
                  "Implicit conversion from InterfacePtr<> (or "
                  "SynchronousInterfacePtr<>) to InterfaceHandle<> requires an rvalue "
                  "reference. Maybe there's a missing std::move(), or consider "
                  "using/providing an InterfaceHandle<> directly instead (particularly "
                  "if the usage prior to this conversion doesn't need to send or receive "
                  "messages).");
    *this = ptr.Unbind();
  }

  // Creates a new channel, retains one endpoint in this |InterfaceHandle| and
  // returns the other as an |InterfaceRequest|.
  //
  // Typically, the returned |InterfaceRequest| is passed to another process,
  // which will implement the server endpoint for the |Interface| protocol.
  //
  // If |NewRequest| fails to create the underlying channel, the returned
  // |InterfaceRequest| will return false from |is_valid()|.
  InterfaceRequest<Interface> NewRequest() {
    zx::channel h1, h2;
    if (zx::channel::create(0, &h1, &h2) != ZX_OK)
      return nullptr;
    channel_ = std::move(h1);
    return InterfaceRequest<Interface>(std::move(h2));
  }

  // Creates an |InterfacePtr| bound to the channel in this |InterfaceHandle|.
  //
  // This function transfers ownership of the underlying channel to the
  // returned |InterfacePtr|, which means the |is_valid()| method will return
  // false after this method returns.
  //
  // Requires the current thread to have a default async_dispatcher_t (e.g., a
  // message loop) in order to read messages from the channel and to monitor the
  // channel for |ZX_CHANNEL_PEER_CLOSED|.
  //
  // Making this method templated ensures that it is not type-instantiated
  // unless it is used, making the InterfacePtr<->InterfaceHandle codependency
  // less fragile.
  template <typename InterfacePtr = InterfacePtr<Interface>>
  inline InterfacePtr Bind() {
    InterfacePtr ptr;
    ptr.Bind(std::move(channel_));
    return ptr;
  }

  template <typename SyncInterfacePtr = SynchronousInterfacePtr<Interface>>
  inline SyncInterfacePtr BindSync() {
    SyncInterfacePtr ptr;
    ptr.Bind(std::move(channel_));
    return ptr;
  }

  // Whether the underlying channel is valid.
  bool is_valid() const { return !!channel_; }
  explicit operator bool() const { return is_valid(); }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(channel_); }

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }
  void set_channel(zx::channel channel) { channel_ = std::move(channel); }

  void Encode(Encoder* encoder, size_t offset) { encoder->EncodeHandle(&channel_, offset); }

  static void Decode(Decoder* decoder, InterfaceHandle<Interface>* value, size_t offset) {
    decoder->DecodeHandle(&value->channel_, offset);
  }

 private:
  zx::channel channel_;
};

// Equality.
template <typename T>
struct Equality<InterfaceHandle<T>> {
  static bool Equals(const InterfaceHandle<T>& lhs, const InterfaceHandle<T>& rhs) {
    return lhs.channel() == rhs.channel();
  }
};

template <typename T>
struct CodingTraits<InterfaceHandle<T>>
    : public EncodableCodingTraits<InterfaceHandle<T>, sizeof(zx_handle_t)> {};

template <typename T>
inline zx_status_t Clone(const InterfaceHandle<T>& value, InterfaceHandle<T>* result) {
  if (!value) {
    *result = InterfaceHandle<T>();
    return ZX_OK;
  }
  return ZX_ERR_ACCESS_DENIED;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERFACE_HANDLE_H_
