// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <queue>
#include <vector>

#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/vocabulary/callback.h"
#include "src/connectivity/overnet/lib/vocabulary/status.h"

namespace overnet {

// Trying to solve the problem that sendclose ack != ready to close
// because we need to finish consuming incoming messages
// INSTEAD we need each side to send a close

class StreamStateListener {
 public:
  // Must ultimately call StreamState::SendCloseAck()
  virtual void SendClose() = 0;
  // Must ultimately call StreamState::QuiesceReady()
  virtual void StreamClosed() = 0;
  // Stop reading
  virtual void StopReading(const Status& final_status) = 0;
};

class StreamState {
 public:
  static constexpr int kMaxCloseRetries = 3;

  explicit StreamState(StreamStateListener* listener);
  ~StreamState();

  void LocalClose(const Status& status, Callback<void> on_quiesced);
  void RemoteClose(const Status& status);
  void SendCloseAck(const Status& status);
  void ForceClose(const Status& status);

  bool IsOpenForSending() const {
    return StateIsOpenForSending(kernel_.state());
  }
  bool IsOpenForReceiving() const {
    return StateIsOpenForReceiving(kernel_.state());
  }
  bool IsClosedForSending() const {
    return StateIsClosedForSending(kernel_.state());
  }
  bool IsClosedForReceiving() const {
    return StateIsClosedForReceiving(kernel_.state());
  }
  bool CanBeginOp() const { return StateCanBeginOp(kernel_.state()); }
  bool CanBeginSend() const { return StateCanBeginSend(kernel_.state()); }

  Status GetSendStatus() const;

  void BeginOp() {
    assert(CanBeginOp());
    ++outstanding_ops_;
  }
  void EndOp() {
    assert(outstanding_ops_ > 0);
    if (0 == --outstanding_ops_) {
      NoOutstandingOps();
    }
  }

  void BeginSend() {
    assert(outstanding_sends_ > 0 || CanBeginSend());
    ++outstanding_sends_;
    BeginOp();
  }
  void EndSend() {
    assert(outstanding_sends_ > 0);
    if (0 == --outstanding_sends_) {
      NoOutstandingSends();
    }
    EndOp();
  }

  std::string Description() const;

  // Called in response to StreamClosed
  void QuiesceReady();

 private:
  enum class State : uint8_t {
    Open,
    LocalCloseRequestedOk,
    LocalCloseRequestedWithError,
    LocalClosedOkDraining,
    LocalClosedOkComplete,
    RemoteClosedOk,
    RemoteClosedAndLocalCloseRequestedOk,
    RemoteClosedAndLocalCloseRequestedWithError,
    RemoteClosedOkAndLocalClosedOkDraining,
    PendingCloseOnSendAck,
    PendingLocalCloseRequestedWithErrorOnSendAck,
    PendingRemoteClosedAndLocalCloseRequestedWithError,
    ClosingProtocol,
    Closed,
    Quiesced
  };

  void NoOutstandingOps();
  void NoOutstandingSends();
  void EnterQuiesced();

  // Projection operators
  static bool StateIsOpenForSending(State state);
  static bool StateIsClosedForSending(State state) {
    return !StateIsOpenForSending(state);
  }
  static bool StateIsOpenForReceiving(State state);
  static bool StateIsClosedForReceiving(State state) {
    return !StateIsOpenForReceiving(state);
  }
  static bool StateIsClosed(State state);
  static bool StateCanBeginSend(State state);
  static bool StateCanBeginOp(State state);
  static bool StateSendsClose(State state);
  static bool StateIsSendAcked(State state);
  static bool StateCarriesError(State state);

  class Kernel {
   public:
    Kernel(StreamStateListener* listener);
    ~Kernel();
    [[nodiscard]] bool ResendClose();
    void SetStateFromAck(State state, Optional<Status> status = Nothing) {
      SetState(state, true, std::move(status));
    }
    void SetStateNoAck(State state, Optional<Status> status = Nothing) {
      SetState(state, false, std::move(status));
    }
    State state() const { return state_; }
    std::string Description() const;
    const Status& ClosingStatus() const;
    void ScheduleQuiesceCallback(Callback<void> on_quiesced);

   private:
    struct Transition {
      State old_state;
      State new_state;
      bool from_ack;
    };

    void SetState(State state, bool from_ack, Optional<Status> status);
    void RunTransition(Transition transition);

    StreamStateListener* const listener_;
    State state_ = State::Open;
    int resends_;
    bool transitioning_ = false;
    Optional<Status> error_;
    std::queue<Transition> pending_transitions_;
    std::vector<Callback<void>> on_quiesced_;
  };

  Kernel kernel_;
  int outstanding_ops_ = 0;
  int outstanding_sends_ = 0;
};

}  // namespace overnet
