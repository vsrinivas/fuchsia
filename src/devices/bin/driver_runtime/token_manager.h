// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_RUNTIME_TOKEN_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_RUNTIME_TOKEN_MANAGER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/token.h>
#include <lib/zx/channel.h>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_wavl_tree.h>

namespace driver_runtime {

class TokenManager {
 public:
  // Id for identifying a token, which consists of a channel pair. We use the koid
  // of the channel end that would be passed to |fdf_token_register|. If |fdf_token_transfer|
  // is called first, we can retrieve the correct koid via the channel's |related_koid|.
  // This is simpler than dealing with 2 different koids for the token channel pair.
  using TokenId = zx_koid_t;

  // Implementation of fdf_token_*.
  zx_status_t Register(zx_handle_t token, fdf_dispatcher_t* dispatcher, fdf_token_t* fdf_token);
  zx_status_t Transfer(zx_handle_t token, fdf_handle_t handle);

  void SetGlobalDispatcher(async_dispatcher_t* dispatcher) {
    // We only expect this to be set once when the DispatcherCoordinator is created.
    ZX_ASSERT(!global_dispatcher_.has_value());
    global_dispatcher_ = dispatcher;
  }

 private:
  // Represents a pending token for which an fdf handle transfer has not yet been completed.
  // This means either |fdf_token_register| or |fdf_token_transfer| has been called, but not both.
  // Once both functions have been called for a token, this object will cease to exist.
  class PendingTokenInfo : public fbl::WAVLTreeContainable<std::unique_ptr<PendingTokenInfo>> {
   public:
    // The state the pending token is in.
    enum class State {
      // The token transfer callback was registered before the transfer was requested.
      kCallbackRegistered,
      // The transfer was requested before the token transfer callback was registered.
      kTransferRequested,
    };

    PendingTokenInfo(State state, TokenId token_id, zx::channel token)
        : state_(state),
          token_id_(token_id),
          token_(std::move(token)),
          wait_(token_.get(), ZX_CHANNEL_PEER_CLOSED) {}

    virtual ~PendingTokenInfo() = default;

    // Required to instantiate fbl::DefaultKeyedObjectTraits.
    TokenId GetKey() const { return token_id_; }

    // Called when a driver registers a token transfer callback.
    virtual zx_status_t OnCallbackRegister(fdf_dispatcher_t* dispatcher,
                                           fdf_token_t* fdf_token) = 0;
    // Called when a driver requests a token transfer for |channel|.
    virtual zx_status_t OnTransferRequest(fdf::Channel channel) = 0;
    // Called when the peer channel handle of |token_| is closed.
    virtual void OnPeerClosed() = 0;

    // Returns the dispatcher stored in the connection state, may be null.
    virtual fdf_dispatcher_t* dispatcher() { return nullptr; }

    State state() const { return state_; }

    TokenId token_id() const { return token_id_; }

    async::Wait& wait() { return wait_; }

   private:
    const State state_;

    // This is used to match a token transfer callback registration with a transfer request, or vice
    // versa. This is koid of the token that would be used to register the callback.
    TokenId token_id_;

    // The token that has been registered with the callback, or provided with the
    // transfer request.
    // This was chosen to be a channel rather than an eventpair, so that in future we could
    // potentially send an epitaph if the channel peer was dropped before the transfer was
    // completed.
    zx::channel token_;

    // This waits for any ZX_CHANNEL_PEER_CLOSED signal on the peer channel handle of |token|.
    // If the signal is received before the transfer is completed, we will drop this
    // |PendingTokenInfo|, and in the case of this being a |RegisteredCallback| we will trigger the
    // callback with |ZX_ERR_CANCELED|.
    async::Wait wait_;
  };

  // A token transfer callback that has been registered by a driver and is awaiting an
  // transfer request.
  class RegisteredCallback : public PendingTokenInfo {
   public:
    RegisteredCallback(TokenId token_id, zx::channel token, fdf_dispatcher_t* dispatcher,
                       fdf_token_t* fdf_token)
        : PendingTokenInfo(State::kCallbackRegistered, token_id, std::move(token)),
          dispatcher_(dispatcher),
          fdf_token_(fdf_token) {}

    // |PendingTokenInfo| implementation.
    zx_status_t OnCallbackRegister(fdf_dispatcher_t* dispatcher, fdf_token_t* fdf_token) override {
      // This should not be called twice for the same token.
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t OnTransferRequest(fdf::Channel channel) override;
    void OnPeerClosed() override;
    fdf_dispatcher_t* dispatcher() override { return dispatcher_; }

   private:
    fdf_dispatcher_t* dispatcher_;
    fdf_token_t* fdf_token_;
  };

  // A token transfer request by a driver that is waiting for a corresponding token transfer
  // callback registration.
  class TransferRequest : public PendingTokenInfo {
   public:
    TransferRequest(TokenId token_id, zx::channel token, fdf::Channel channel)
        : PendingTokenInfo(State::kTransferRequested, token_id, std::move(token)),
          channel_(std::move(channel)) {}

    // |PendingTokenInfo| implementation.
    zx_status_t OnCallbackRegister(fdf_dispatcher_t* dispatcher, fdf_token_t* fdf_token) override;
    zx_status_t OnTransferRequest(fdf::Channel channel) override {
      // This should not be called twice for the same token.
      return ZX_ERR_BAD_STATE;
    }
    void OnPeerClosed() override {
      // The protocol manager will remove us from |pending_tokens_|, but we don't need
      // to do anything extra here. Since the transfer was not completed,
      // the fdf |channel_| will be closed, and the client will find out the transfer
      // failed once it reads or writes from their end of the fdf channel.
    }

   private:
    // TODO(fxbug.dev/105578): replace fdf::Channel with a generic C++ handle type when available.
    fdf::Channel channel_;
  };

  // Registers an |async::Wait| to listen for |ZX_CHANNEL_PEER_CLOSED| signals on the
  // peer channel handle of the pending token's handle.
  zx_status_t WaitOnPeerClosedLocked(PendingTokenInfo* pending_token) __TA_REQUIRES(&lock_);

  async_dispatcher_t* global_dispatcher() {
    ZX_ASSERT(global_dispatcher_.has_value());
    return global_dispatcher_.value();
  }

  std::optional<async_dispatcher_t*> global_dispatcher_;

  fbl::Mutex lock_;
  // Maps from token id to the pending token.
  fbl::WAVLTree<TokenId, std::unique_ptr<PendingTokenInfo>> pending_tokens_ __TA_GUARDED(lock_);
};

}  // namespace driver_runtime

#endif  //  SRC_DEVICES_BIN_DRIVER_RUNTIME_TOKEN_MANAGER_H_
