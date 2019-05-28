// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/datagram_stream/stream_state.h"

#include <assert.h>

#include "src/connectivity/overnet/lib/environment/trace.h"

namespace overnet {

StreamState::Kernel::Kernel(StreamStateListener* listener)
    : listener_(listener) {}

StreamState::Kernel::~Kernel() {
  assert(state_ == State::Quiesced);
  assert(!transitioning_);
}

StreamState::StreamState(StreamStateListener* listener) : kernel_(listener) {}

StreamState::~StreamState() { assert(outstanding_ops_ == 0); }

///////////////////////////////////////////////////////////////////////////////
// Event handlers

void StreamState::LocalClose(const Status& status, Callback<void> on_quiesced) {
  OVERNET_TRACE(DEBUG) << "LocalClose: " << status
                       << " state=" << Description();
  auto st = kernel_.state();
  kernel_.ScheduleQuiesceCallback(std::move(on_quiesced));
  switch (st) {
    case State::Open:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::LocalCloseRequestedWithError, status);
      } else {
        kernel_.SetStateNoAck(State::LocalCloseRequestedOk);
      }
      break;
    case State::LocalCloseRequestedOk:
      if (status.is_error()) {
        kernel_.SetStateNoAck(
            State::PendingLocalCloseRequestedWithErrorOnSendAck, status);
      }
      break;
    case State::LocalClosedOkDraining:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::LocalCloseRequestedWithError, status);
      }
      break;
    case State::LocalClosedOkComplete:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::LocalCloseRequestedWithError, status);
      }
      break;
    case State::RemoteClosedOk:
      if (status.is_ok()) {
        kernel_.SetStateNoAck(State::RemoteClosedAndLocalCloseRequestedOk);
      } else {
        kernel_.SetStateNoAck(
            State::RemoteClosedAndLocalCloseRequestedWithError, status);
      }
      break;
    case State::RemoteClosedAndLocalCloseRequestedOk:
      if (status.is_error()) {
        kernel_.SetStateNoAck(
            State::PendingRemoteClosedAndLocalCloseRequestedWithError, status);
      }
      break;
    case State::RemoteClosedOkAndLocalClosedOkDraining:
      if (status.is_error()) {
        kernel_.SetStateNoAck(
            State::RemoteClosedAndLocalCloseRequestedWithError, status);
      }
      break;
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingCloseOnSendAck:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      break;
  }
}

void StreamState::ForceClose(const Status& status) {
  OVERNET_TRACE(DEBUG) << "LocalClose: " << status
                       << " state=" << Description();
  switch (kernel_.state()) {
    case State::Open:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
      kernel_.SetStateNoAck(State::ClosingProtocol);
      break;
    case State::LocalCloseRequestedOk:
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      break;
    case State::RemoteClosedOk:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
      kernel_.SetStateNoAck(State::ClosingProtocol);
      break;
    case State::PendingCloseOnSendAck:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      break;
  }
}

void StreamState::SendCloseAck(const Status& status) {
  OVERNET_TRACE(DEBUG) << "SendCloseAck: " << status
                       << " state=" << Description();
  switch (kernel_.state()) {
    case State::LocalCloseRequestedOk:
      if (status.is_ok()) {
        if (outstanding_sends_ > 0) {
          kernel_.SetStateFromAck(State::LocalClosedOkDraining);
        } else {
          kernel_.SetStateFromAck(State::LocalClosedOkComplete);
        }
        break;
      }
      if (status.code() == StatusCode::UNAVAILABLE && kernel_.ResendClose()) {
        break;
      }
      kernel_.SetStateFromAck(State::ClosingProtocol);
      break;
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
      if (status.code() == StatusCode::UNAVAILABLE && kernel_.ResendClose()) {
        break;
      }
      kernel_.SetStateFromAck(State::ClosingProtocol);
      break;
    case State::PendingCloseOnSendAck:
      kernel_.SetStateFromAck(State::ClosingProtocol);
      break;
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
      kernel_.SetStateFromAck(State::LocalCloseRequestedWithError);
      break;
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      kernel_.SetStateFromAck(
          State::RemoteClosedAndLocalCloseRequestedWithError);
      break;
    case State::Open:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedOk:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      abort();
  }
}

void StreamState::RemoteClose(const Status& status) {
  OVERNET_TRACE(DEBUG) << "RemoteClose: " << status
                       << " state=" << Description();
  switch (kernel_.state()) {
    case State::Open:
      if (status.is_ok()) {
        kernel_.SetStateNoAck(State::RemoteClosedOk);
      } else {
        kernel_.SetStateNoAck(State::ClosingProtocol);
      }
      break;
    case State::LocalCloseRequestedOk:
      if (status.is_ok()) {
        kernel_.SetStateNoAck(State::RemoteClosedAndLocalCloseRequestedOk);
      } else {
        kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      }
      break;
    case State::LocalCloseRequestedWithError:
      if (status.is_ok()) {
        kernel_.SetStateNoAck(
            State::RemoteClosedAndLocalCloseRequestedWithError);
      } else {
        kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      }
      break;
    case State::LocalClosedOkDraining:
      if (status.is_ok()) {
        kernel_.SetStateNoAck(State::RemoteClosedOkAndLocalClosedOkDraining);
      } else {
        kernel_.SetStateNoAck(State::ClosingProtocol);
      }
      break;
    case State::LocalClosedOkComplete:
      kernel_.SetStateNoAck(State::ClosingProtocol);
      break;
    case State::RemoteClosedOk:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::ClosingProtocol);
      }
      break;
    case State::RemoteClosedAndLocalCloseRequestedOk:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      }
      break;
    case State::RemoteClosedAndLocalCloseRequestedWithError:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      }
      break;
    case State::RemoteClosedOkAndLocalClosedOkDraining:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::ClosingProtocol);
      }
      break;
    case State::PendingCloseOnSendAck:
      break;
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      } else {
        kernel_.SetStateNoAck(
            State::PendingRemoteClosedAndLocalCloseRequestedWithError);
      }
      break;
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      if (status.is_error()) {
        kernel_.SetStateNoAck(State::PendingCloseOnSendAck);
      }
      break;
    case State::ClosingProtocol:
      break;
    case State::Closed:
      break;
    case State::Quiesced:
      break;
  }
}

void StreamState::QuiesceReady() {
  OVERNET_TRACE(DEBUG) << "QuiesceReady: state=" << Description();
  assert(kernel_.state() == State::ClosingProtocol);
  kernel_.SetStateNoAck(State::Closed);
  if (outstanding_ops_ == 0) {
    EnterQuiesced();
  }
}

void StreamState::NoOutstandingOps() {
  OVERNET_TRACE(DEBUG) << "NoOutstandingOps: state=" << Description();
  assert(outstanding_ops_ == 0);
  if (kernel_.state() == State::Closed) {
    EnterQuiesced();
  }
}

void StreamState::NoOutstandingSends() {
  OVERNET_TRACE(DEBUG) << "NoOutstandingSends: state=" << Description();
  assert(outstanding_sends_ == 0);
  switch (kernel_.state()) {
    case State::Open:
    case State::LocalCloseRequestedOk:
    case State::LocalCloseRequestedWithError:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingCloseOnSendAck:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      break;
    case State::LocalClosedOkDraining:
      kernel_.SetStateNoAck(State::LocalClosedOkComplete);
      break;
    case State::RemoteClosedOkAndLocalClosedOkDraining:
      kernel_.SetStateNoAck(State::ClosingProtocol);
      break;
  }
}

void StreamState::EnterQuiesced() {
  assert(kernel_.state() == State::Closed);
  kernel_.SetStateNoAck(State::Quiesced);
}

Status StreamState::GetSendStatus() const {
  assert(StateIsSendAcked(kernel_.state()));
  return kernel_.ClosingStatus();
}

///////////////////////////////////////////////////////////////////////////////
// Transition operations

void StreamState::Kernel::SetState(State new_state, bool from_ack,
                                   Optional<Status> status) {
  const auto old_state = state_;
  state_ = new_state;

  if (StateCarriesError(new_state)) {
    if (!error_.has_value()) {
      assert(status.has_value());
      assert(status->is_error());
      error_ = status;
    }
  } else {
    assert(!status.has_value());
  }

  OVERNET_TRACE(DEBUG) << "State changes to: " << Description();

  if (transitioning_) {
    pending_transitions_.push(Transition{old_state, new_state, from_ack});
  } else {
    transitioning_ = true;
    RunTransition(Transition{old_state, new_state, from_ack});
    while (!pending_transitions_.empty()) {
      auto t = pending_transitions_.front();
      pending_transitions_.pop();
      RunTransition(t);
    }
    transitioning_ = false;

    if (state_ == State::Quiesced) {
      std::vector<Callback<void>> on_quiesced;
      on_quiesced.swap(on_quiesced_);
      on_quiesced.clear();
    }
  }
}

void StreamState::Kernel::RunTransition(Transition transition) {
  if (transition.from_ack) {
    assert(StateIsSendAcked(transition.old_state));
  }

  if (StateSendsClose(transition.new_state)) {
    assert(StateIsSendAcked(transition.new_state));
    if (transition.from_ack || !StateIsSendAcked(transition.old_state)) {
      resends_ = 0;
      listener_->SendClose();
    }
  }

  if (StateIsClosedForReceiving(transition.new_state)) {
    if (!StateIsClosedForReceiving(transition.old_state)) {
      listener_->StopReading(ClosingStatus());
    }
  } else {
    assert(!StateIsClosedForReceiving(transition.old_state));
  }

  if (StateIsClosed(transition.new_state)) {
    assert(StateIsClosedForReceiving(transition.new_state));
    if (!StateIsClosed(transition.old_state)) {
      listener_->StreamClosed();
    }
  } else {
    assert(!StateIsClosed(transition.old_state));
  }
}

bool StreamState::Kernel::ResendClose() {
  assert(StateSendsClose(state_) && StateIsSendAcked(state_));
  ++resends_;
  if (resends_ > kMaxCloseRetries) {
    return false;
  }
  listener_->SendClose();
  return true;
}

const Status& StreamState::Kernel::ClosingStatus() const {
  if (StateCarriesError(state_)) {
    return *error_;
  } else {
    return Status::Ok();
  }
}

void StreamState::Kernel::ScheduleQuiesceCallback(Callback<void> callback) {
  if (state_ == State::Quiesced && !transitioning_) {
    callback();
    return;
  }
  on_quiesced_.emplace_back(std::move(callback));
}

///////////////////////////////////////////////////////////////////////////////
// Projection operators

bool StreamState::StateIsOpenForSending(State state) {
  switch (state) {
    case State::Open:
    case State::LocalCloseRequestedOk:
    case State::LocalClosedOkDraining:
    case State::RemoteClosedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
      return true;
    case State::LocalCloseRequestedWithError:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingCloseOnSendAck:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      return false;
  }
}

bool StreamState::StateIsOpenForReceiving(State state) {
  switch (state) {
    case State::Open:
    case State::LocalCloseRequestedOk:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
      return true;
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingCloseOnSendAck:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      return false;
  }
}

bool StreamState::StateCanBeginSend(State state) {
  switch (state) {
    case State::Open:
    case State::RemoteClosedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
      return true;
    case State::LocalCloseRequestedOk:
    case State::LocalCloseRequestedWithError:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingCloseOnSendAck:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      return false;
  }
}

bool StreamState::StateIsClosed(State state) {
  switch (state) {
    case State::Open:
    case State::LocalCloseRequestedOk:
    case State::LocalCloseRequestedWithError:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::PendingCloseOnSendAck:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      return false;
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      return true;
  }
}

bool StreamState::StateCanBeginOp(State state) {
  return state != State::Quiesced;
}

bool StreamState::StateIsSendAcked(State state) {
  switch (state) {
    case State::Open:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedOk:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      return false;
    case State::LocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingCloseOnSendAck:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      return true;
  }
}

bool StreamState::StateSendsClose(State state) {
  switch (state) {
    case State::Open:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedOk:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
    case State::PendingCloseOnSendAck:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      return false;
    case State::LocalCloseRequestedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
      return true;
  }
}

bool StreamState::StateCarriesError(State state) {
  switch (state) {
    case State::Open:
    case State::LocalCloseRequestedOk:
    case State::LocalClosedOkDraining:
    case State::LocalClosedOkComplete:
    case State::RemoteClosedOk:
    case State::RemoteClosedAndLocalCloseRequestedOk:
    case State::RemoteClosedOkAndLocalClosedOkDraining:
    case State::PendingCloseOnSendAck:
    case State::ClosingProtocol:
    case State::Closed:
    case State::Quiesced:
      return false;
    case State::LocalCloseRequestedWithError:
    case State::RemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
      return true;
  }
}

///////////////////////////////////////////////////////////////////////////////
// State description printers

std::string StreamState::Description() const {
  std::ostringstream out;

  out << kernel_.Description();

  if (outstanding_sends_) {
    out << "+" << outstanding_sends_ << "sends";
  }
  if (outstanding_ops_) {
    out << "+" << outstanding_ops_ << "ops";
  }

  return out.str();
}

std::string StreamState::Kernel::Description() const {
  std::ostringstream out;

  switch (state_) {
    case State::Open:
      out << "Open";
      break;
    case State::LocalCloseRequestedOk:
      out << "LocalCloseRequestedOk";
      break;
    case State::LocalCloseRequestedWithError:
      out << "LocalCloseRequestedWithError";
      break;
    case State::LocalClosedOkDraining:
      out << "LocalClosedOkDraining";
      break;
    case State::LocalClosedOkComplete:
      out << "LocalClosedOkComplete";
      break;
    case State::RemoteClosedOk:
      out << "RemoteClosedOk";
      break;
    case State::RemoteClosedAndLocalCloseRequestedOk:
      out << "RemoteClosedAndLocalCloseRequestedOk";
      break;
    case State::RemoteClosedAndLocalCloseRequestedWithError:
      out << "RemoteClosedAndLocalCloseRequestedWithError";
      break;
    case State::RemoteClosedOkAndLocalClosedOkDraining:
      out << "RemoteClosedOkAndLocalClosedOkDraining";
      break;
    case State::PendingCloseOnSendAck:
      out << "PendingCloseOnSendAck";
      break;
    case State::PendingLocalCloseRequestedWithErrorOnSendAck:
      out << "PendingLocalCloseRequestedWithErrorOnSendAck";
      break;
    case State::PendingRemoteClosedAndLocalCloseRequestedWithError:
      out << "PendingRemoteClosedAndLocalCloseRequestedWithError";
      break;
    case State::ClosingProtocol:
      out << "ClosingProtocol";
      break;
    case State::Closed:
      out << "Closed";
      break;
    case State::Quiesced:
      out << "Quiesced";
      break;
  }
  if (error_.has_value()) {
    out << ":" << *error_;
  }
  if (!on_quiesced_.empty()) {
    out << "+" << on_quiesced_.size() << "Qs";
  }

  return out.str();
}

}  // namespace overnet
