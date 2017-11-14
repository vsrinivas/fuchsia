// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/router.h"

#include <string>
#include <utility>

#include "lib/fidl/cpp/bindings/message_validator.h"
#include "lib/fxl/logging.h"

namespace fidl {
namespace internal {

// ----------------------------------------------------------------------------

class ResponderThunk : public MessageReceiverWithStatus {
 public:
  explicit ResponderThunk(const SharedData<Router*>& router)
      : router_(router), accept_was_invoked_(false) {}
  ~ResponderThunk() override {
    if (!accept_was_invoked_) {
      // The Mojo application handled a message that was expecting a response
      // but did not send a response.
      Router* router = router_.value();
      if (router) {
        // We close the channel here as a way of signaling to the calling
        // application that an error condition occurred. Without this the
        // calling application would have no way of knowing it should stop
        // waiting for a response.
        router->CloseChannel();
      }
    }
  }

  // MessageReceiver implementation:
  bool Accept(Message* message) override {
    accept_was_invoked_ = true;
    FXL_DCHECK(message->has_flag(kMessageIsResponse));

    bool result = false;

    Router* router = router_.value();
    if (router)
      result = router->Accept(message);

    return result;
  }

  // MessageReceiverWithStatus implementation:
  bool IsValid() override {
    Router* router = router_.value();
    return router && !router->encountered_error() && router->is_valid();
  }

 private:
  SharedData<Router*> router_;
  bool accept_was_invoked_;
};

// ----------------------------------------------------------------------------

Router::HandleIncomingMessageThunk::HandleIncomingMessageThunk(Router* router)
    : router_(router) {}

Router::HandleIncomingMessageThunk::~HandleIncomingMessageThunk() {}

bool Router::HandleIncomingMessageThunk::Accept(Message* message) {
  return router_->HandleIncomingMessage(message);
}

// ----------------------------------------------------------------------------

Router::Router(zx::channel channel, MessageValidatorList validators)
    : thunk_(this),
      validators_(std::move(validators)),
      connector_(std::move(channel)),
      weak_self_(this),
      incoming_receiver_(nullptr),
      next_request_id_(0),
      testing_mode_(false) {
  // This receiver thunk redirects to Router::HandleIncomingMessage.
  connector_.set_incoming_receiver(&thunk_);
}

Router::~Router() {
  weak_self_.set_value(nullptr);

  for (ResponderMap::const_iterator i = responders_.begin();
       i != responders_.end(); ++i) {
    delete i->second;
  }
}

bool Router::Accept(Message* message) {
  FXL_DCHECK(!message->has_flag(kMessageExpectsResponse));
  return connector_.Accept(message);
}

bool Router::AcceptWithResponder(Message* message, MessageReceiver* responder) {
  FXL_DCHECK(message->has_flag(kMessageExpectsResponse));

  // Reserve 0 in case we want it to convey special meaning in the future.
  uint64_t request_id = next_request_id_++;
  if (request_id == 0)
    request_id = next_request_id_++;

  message->set_request_id(request_id);
  if (!connector_.Accept(message))
    return false;

  // We assume ownership of |responder|.
  responders_[request_id] = responder;
  return true;
}

void Router::EnableTestingMode() {
  testing_mode_ = true;
  connector_.set_enforce_errors_from_incoming_receiver(false);
}

bool Router::HandleIncomingMessage(Message* message) {
  std::string* err = nullptr;
#ifndef NDEBUG
  std::string err2;
  err = &err2;
#endif

  ValidationError result = RunValidatorsOnMessage(validators_, message, err);
  if (result != ValidationError::NONE)
    return false;

  if (message->has_flag(kMessageExpectsResponse)) {
    if (incoming_receiver_) {
      MessageReceiverWithStatus* responder = new ResponderThunk(weak_self_);
      bool ok = incoming_receiver_->AcceptWithResponder(message, responder);
      if (!ok)
        delete responder;
      return ok;
    }

    // If we receive a request expecting a response when the client is not
    // listening, then we have no choice but to tear down the channel.
    connector_.CloseChannel();
  } else if (message->has_flag(kMessageIsResponse)) {
    uint64_t request_id = message->request_id();
    ResponderMap::iterator it = responders_.find(request_id);
    if (it == responders_.end()) {
      FXL_DCHECK(testing_mode_);
      return false;
    }
    MessageReceiver* responder = it->second;
    responders_.erase(it);
    bool ok = responder->Accept(message);
    delete responder;
    return ok;
  } else {
    if (incoming_receiver_)
      return incoming_receiver_->Accept(message);
    // OK to drop message on the floor.
  }

  return false;
}

// ----------------------------------------------------------------------------

}  // namespace internal
}  // namespace fidl
