// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_PENDING_RESPONSE_H_
#define LIB_FIDL_CPP_INTERNAL_PENDING_RESPONSE_H_

#include <lib/fidl/cpp/message_builder.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {
class WeakStubController;

// A response to a FIDL message.
//
// When a server receives a message that expects a response, the stub receives a
// |PendingResponse| object that the implementation can use to reply to the
// message. A given |PendingResponse| object can be used to send a reply at
// most once.
//
// If the |StubController| that processed the original message is destroyed or
// unbound from the underlying channel (e.g., due to an error), the stub can
// still safely call |Send|, but the response will not actually be sent to the
// client.
class PendingResponse {
 public:
  // Creates a |PendingResponse| that does not need a response.
  //
  // The |needs_response()| method will return false.
  PendingResponse();

  // Creates a |PendingResponse| for a message with the given transaction ID.
  //
  // The |PendingResponse| object will take a reference to |weak_controller|,
  // which it releases in its destructor.
  PendingResponse(zx_txid_t txid, WeakStubController* weak_controller);

  ~PendingResponse();

  // |PendingResponse| objects are copiable.
  //
  // Each copy refers to the same logical reponse, which means |Send| should be
  // called at most once among all the copies.
  //
  // The reason |PendingResponse| objects are copiable is so that they can be
  // held by an std::function, which is also copyable. Typically, a
  // |PendingResponse| object is held as a member of another object that
  // implements operator(), which can be wrapped by std::function.
  PendingResponse(const PendingResponse& other);
  PendingResponse& operator=(const PendingResponse& other);

  // |PendingResponse| objects are movable.
  //
  // Moving a |PendingResponse| object is more efficient that copying it because
  // moving avoid churning the reference count of the associated
  // |WeakStubController|.
  PendingResponse(PendingResponse&& other);
  PendingResponse& operator=(PendingResponse&& other);

  // Whether the message that caused this |PendingResponse| object to be created
  // expects a response.
  //
  // This method does not indiciate whether a response has or has not already
  // been sent. That state is difficult to track because |PendingResponse| is
  // copiable.
  bool needs_response() const { return txid_; }

  // Send a response.
  //
  // This function should be called at most once among all the copies of a given
  // |PendingResponse| object.
  //
  // If the associated |WeakStubController| is no longer available (e.g., if it
  // has been destroyed), this function will return |ZX_ERR_BAD_STATE|.
  zx_status_t Send(const fidl_type_t* type, Message message);

 private:
  // This class should be small enough to fit into the inline storage for an
  // std::function to avoid allocating additional storage when processing
  // messages. Currently, std::function has space for three pointers.
  zx_txid_t txid_;
  WeakStubController* weak_controller_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_PENDING_RESPONSE_H_
