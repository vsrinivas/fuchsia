// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERFACE_REQUEST_H_
#define LIB_FIDL_CPP_INTERFACE_REQUEST_H_

#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <cstddef>
#include <utility>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/coding_traits.h"

namespace fidl {
class Builder;

// The server endpoint of a FIDL channel.
//
// The remote end of the channel expects this end of the channel to speak the
// protocol associated with |Interface|. This type is the dual of
// |InterfaceHandle|.
//
// An |InterfaceRequest| does not have thread affinity and can therefore be
// transferred to another thread or another process. To bind an implementation
// of |Interface| to this |InterfaceRequest|, use a |Binding| object.
//
// Typically, |InterfaceRequest| objects are created by a prospective client of
// |Interface|, which then sends the |InterfaceRequest| to another process to
// request that the remote process implement the |Interface|. This pattern
// enables *pipelined* operation, in which the client can start calling methods
// on an associated |InterfacePtr| immediately, before the |InterfaceRequest|
// has reached the remote process and been bound to an implementation. These
// method calls are buffered by the underlying channel until they are read by
// the remote process.
//
// Example:
//
//   #include "foo.fidl.h"
//
//   class FooImpl : public Foo {
//    public:
//     explicit FooImpl(InterfaceRequest<Foo> request)
//         : binding_(this, std::move(request)) {}
//
//     // Foo implementation here.
//
//    private:
//     Binding<Foo> binding_;
//   };
//
// After the |InterfaceRequest| has been bound to an implementation, the
// implementation will receive method calls from the remote endpoint of the
// channel on the thread on which the |InterfaceRequest| was bound.
//
// See also:
//
//  * |InterfaceHandle|, which is the client analog of an |InterfaceRequest|.
template <typename Interface>
class InterfaceRequest {
 public:
  // Creates an |InterfaceHandle| whose underlying channel is invalid.
  //
  // Some protocols contain messages that permit such |InterfaceRequest|
  // objects, which indicate that the client is not interested in the server
  // providing an implementation of |Interface|.
  InterfaceRequest() = default;

  // Creates an |InterfaceHandle| that wraps the given |channel|.
  explicit InterfaceRequest(zx::channel channel)
      : channel_(std::move(channel)) {}

  InterfaceRequest(const InterfaceRequest& other) = delete;
  InterfaceRequest& operator=(const InterfaceRequest& other) = delete;

  InterfaceRequest(InterfaceRequest&& other)
      : channel_(std::move(other.channel_)) {}

  InterfaceRequest& operator=(InterfaceRequest&& other) {
    channel_ = std::move(other.channel_);
    return *this;
  }

  // Implicit conversion from nullptr to an |InterfaceRequest| without an
  // invalid |channel|.
  InterfaceRequest(std::nullptr_t) {}

  // Whether the underlying channel is valid.
  bool is_valid() const { return !!channel_; }
  explicit operator bool() const { return is_valid(); }

  // Transfers ownership of the underlying channel to the caller.
  zx::channel TakeChannel() { return std::move(channel_); }

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }
  void set_channel(zx::channel channel) { channel_ = std::move(channel); }

  void Encode(Encoder* encoder, size_t offset) {
    encoder->EncodeHandle(&channel_, offset);
  }

  static void Decode(Decoder* decoder, InterfaceRequest<Interface>* value,
                     size_t offset) {
    decoder->DecodeHandle(&value->channel_, offset);
  }

 private:
  zx::channel channel_;
};

// A |InterfaceRequestHandler<Interface>| is simply a function that
// handles an interface request for |Interface|. If it determines that the
// request should be "accepted", then it should "connect" ("take ownership
// of") request. Otherwise, it can simply drop |request| (as implied by the
// interface).
template <typename Interface>
using InterfaceRequestHandler =
    fit::function<void(fidl::InterfaceRequest<Interface> request)>;

// Equality.
template <typename T>
bool operator==(const InterfaceRequest<T>& lhs,
                const InterfaceRequest<T>& rhs) {
  return lhs.channel() == rhs.channel();
}
template <typename T>
bool operator!=(const InterfaceRequest<T>& lhs,
                const InterfaceRequest<T>& rhs) {
  return !(lhs == rhs);
}

// Comparaisons.
template <typename T>
bool operator<(const InterfaceRequest<T>& lhs, const InterfaceRequest<T>& rhs) {
  return lhs.channel() < rhs.channel();
}
template <typename T>
bool operator>(const InterfaceRequest<T>& lhs, const InterfaceRequest<T>& rhs) {
  return lhs.channel() > rhs.channel();
}
template <typename T>
bool operator<=(const InterfaceRequest<T>& lhs,
                const InterfaceRequest<T>& rhs) {
  return !(lhs > rhs);
}
template <typename T>
bool operator>=(const InterfaceRequest<T>& lhs,
                const InterfaceRequest<T>& rhs) {
  return !(lhs < rhs);
}

template <typename T>
struct CodingTraits<InterfaceRequest<T>>
    : public EncodableCodingTraits<InterfaceRequest<T>, sizeof(zx_handle_t)> {};

template <typename T>
inline zx_status_t Clone(const InterfaceRequest<T>& value,
                         InterfaceRequest<T>* result) {
  if (!value) {
    *result = InterfaceRequest<T>();
    return ZX_OK;
  }
  return ZX_ERR_ACCESS_DENIED;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERFACE_REQUEST_H_
