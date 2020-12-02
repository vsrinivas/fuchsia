// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_SYNCHRONOUS_PROXY_H_
#define LIB_FIDL_CPP_INTERNAL_SYNCHRONOUS_PROXY_H_

#include <lib/fidl/cpp/internal/message_sender.h>
#include <lib/fidl/cpp/message.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace fidl {
namespace internal {

// Manages the client state for a synchronous interface.
//
// A |SynchronousProxy| manages the client state for a sychronous interface.
// This object validates messages before sending them to the remote endpoint,
// and (optionally) blocks until it receives a reply.
//
// Instances of this class are thread-safe.
class SynchronousProxy final : public MessageSender {
 public:
  // Creates a |SynchronousProxy| that wraps the given channel.
  explicit SynchronousProxy(zx::channel channel);
  ~SynchronousProxy();

  // Returns the underlying channel from this object.
  //
  // The |SynchronousProxy| does not attempt to synchronize this operation with
  // |Send| or |Call|.
  zx::channel TakeChannel();

  // Validates that |message| matches the given |type| and sends the message
  // through the underlying channel.
  //
  // Does not block.
  //
  // Returns an error if validation or writing fails.
  zx_status_t Send(const fidl_type_t* type, HLCPPOutgoingMessage message) final;

  // Validate that |request| matches the given |request_type| and sends
  // |request| through the underlying channel. Blocks until it receives a
  // response, which is then decoded according to |response_type| and returned
  // in |response_type|.
  //
  // Blocks until the remote endpoint replied.
  //
  // Returns an error if validation, writing, reading, or decoding fails.
  zx_status_t Call(const fidl_type_t* request_type, const fidl_type_t* response_type,
                   HLCPPOutgoingMessage request, HLCPPIncomingMessage* response);

  // The underlying channel.
  const zx::channel& channel() const { return channel_; }

 private:
  zx::channel channel_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_SYNCHRONOUS_PROXY_H_
