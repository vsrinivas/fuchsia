// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "token_manager.h"

#include <lib/zx/channel.h>

#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/bin/driver_runtime/handle.h"

namespace driver_runtime {

namespace {

// Verifies |token| is a valid channel handle, and returns the corresponding token id.
// If |use_primary_koid| is true, the token id will be the koid of |token|, otherwise it will
// be the koid of the peer of |token|.
zx::result<TokenManager::TokenId> ValidateToken(zx_handle_t token, bool use_primary_koid) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(token, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }
  if (info.type != ZX_OBJ_TYPE_CHANNEL) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }
  TokenManager::TokenId token_id = use_primary_koid ? info.koid : info.related_koid;
  return zx::ok(token_id);
}

}  // namespace

zx_status_t TokenManager::Register(zx_handle_t token, fdf_dispatcher_t* dispatcher,
                                   fdf_token_t* fdf_token) {
  auto validate_status = ValidateToken(token, true /* use_primary_koid */);
  if (!validate_status.is_ok()) {
    zx_handle_close(token);
    return validate_status.status_value();
  }
  TokenId token_id = *validate_status;
  zx::channel validated_token(token);

  if (!dispatcher || !fdf_token) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  // If registering with the dispatcher fails, we will drop our token handle,
  // and the peer token handle will be notified of ZX_CHANNEL_PEER_CLOSED,
  zx_status_t status = dispatcher->RegisterPendingToken(fdf_token);
  if (status != ZX_OK) {
    return status;
  }

  auto request = pending_tokens_.find(token_id);
  if (request.IsValid()) {
    // An exchange request matching our |token_id| was previously requested,
    // schedule the exchange callback to be called now.
    auto pending_token_info = pending_tokens_.erase(request);
    ZX_ASSERT(pending_token_info);
    ZX_ASSERT(pending_token_info->state() == PendingTokenInfo::State::kExchangeRequested);
    return pending_token_info->OnCallbackRegister(dispatcher, fdf_token);
  }

  // No exchange has been requested for this |token_id| yet.
  auto pending_token_info = std::make_unique<RegisteredCallback>(
      token_id, std::move(validated_token), dispatcher, fdf_token);
  // Listen for peer token handle closed in case they drop their token.
  // It is safe to do this before inserting |pending_token_info| into the map as we are holding
  // |lock_|.
  status = WaitOnPeerClosedLocked(pending_token_info.get());
  if (status != ZX_OK) {
    return status;
  }
  pending_tokens_.insert(std::move(pending_token_info));
  return ZX_OK;
}

zx_status_t TokenManager::Exchange(zx_handle_t token, fdf_handle_t handle) {
  // Retrieve the token id using the koid of the channel peer, so we can locate the
  // corresponding registered protocol.
  auto validate_status = ValidateToken(token, false /* use_primary_koid */);
  if (!validate_status.is_ok()) {
    zx_handle_close(token);
    fdf_handle_close(handle);
    return validate_status.status_value();
  }
  TokenId token_id = *validate_status;
  zx::channel validated_token(token);

  fbl::AutoLock lock(&lock_);

  // TODO(fxbug.dev/86309): we should also check the correct driver owns the handle once possible.
  if (!Handle::HandleExists(handle)) {
    return ZX_ERR_BAD_HANDLE;
  }

  // TODO(fxbug.dev/105578): replace fdf::Channel with a generic C++ handle type when available.
  fdf::Channel validated_fdf_channel = fdf::Channel(handle);

  auto registered_callback = pending_tokens_.find(token_id);
  if (registered_callback.IsValid()) {
    // A token exchange callback matching our token was previously registered, schedule it to
    // be called.
    auto pending_token_info = pending_tokens_.erase(registered_callback);
    ZX_ASSERT(pending_token_info);
    ZX_ASSERT(pending_token_info->state() == PendingTokenInfo::State::kCallbackRegistered);
    return pending_token_info->OnExchangeRequest(std::move(validated_fdf_channel));
  }

  auto pending_token_info = std::make_unique<ExchangeRequest>(token_id, std::move(validated_token),
                                                              std::move(validated_fdf_channel));
  // Listen for peer token handle closed in case they drop their token.
  // It is safe to do this before inserting |pending_token_info| into the map as we are holding
  // |lock_|.
  zx_status_t status = WaitOnPeerClosedLocked(pending_token_info.get());
  if (status != ZX_OK) {
    return status;
  }
  pending_tokens_.insert(std::move(pending_token_info));
  return ZX_OK;
}

zx_status_t TokenManager::WaitOnPeerClosedLocked(PendingTokenInfo* pending_token) {
  // For token exchange callback registrations, we want to use the dispatcher provided,
  // so that we will be automatically notified if the dispatcher shuts down.
  // For token exchange requests, no dispatcher is provided, so we use the global dispatcher.
  async_dispatcher_t* dispatcher =
      pending_token->dispatcher() ? pending_token->dispatcher() : global_dispatcher();
  ZX_ASSERT(dispatcher != nullptr);

  pending_token->wait().set_handler([this, pending_token](async_dispatcher_t* dispatcher,
                                                          async::Wait* wait, zx_status_t status,
                                                          const zx_packet_signal_t* signal) {
    fbl::AutoLock lock(&lock_);

    if (status == ZX_OK) {
      if (signal->trigger & ZX_CHANNEL_PEER_CLOSED) {
        pending_token->OnPeerClosed();
      }
    } else {
      ZX_ASSERT_MSG(status == ZX_ERR_CANCELED, "WaitOnPeerClosed got unexpected error %d", status);
      // If the wait is cancelled due to a dispatcher shutting down, the dispatcher will
      // handle calling the client's handler in |Dispatcher::CompleteShutdown|.
    }
    ZX_ASSERT(pending_tokens_.erase(pending_token->token_id()) != nullptr);
  });
  return pending_token->wait().Begin(dispatcher);
}

void TokenManager::RegisteredCallback::OnPeerClosed() {
  ZX_ASSERT(fdf_token_ != nullptr);
  ZX_ASSERT(dispatcher_ != nullptr);
  auto status = dispatcher_->ScheduleTokenCallback(fdf_token_, ZX_ERR_CANCELED, fdf::Channel());
  // This may fail if the dispatcher is shutting down. In that case the dispatcher is
  // going to send the cancellation callback in |CompleteShutdown|.
  ZX_ASSERT((status == ZX_OK) || (status == ZX_ERR_BAD_STATE));
}

zx_status_t TokenManager::RegisteredCallback::OnExchangeRequest(fdf::Channel channel) {
  ZX_ASSERT(channel.is_valid());
  ZX_ASSERT(fdf_token_ != nullptr);
  ZX_ASSERT(dispatcher_ != nullptr);
  return dispatcher_->ScheduleTokenCallback(fdf_token_, ZX_OK, std::move(channel));
}

zx_status_t TokenManager::ExchangeRequest::OnCallbackRegister(fdf_dispatcher_t* dispatcher,
                                                              fdf_token_t* fdf_token) {
  ZX_ASSERT(channel_.is_valid());
  ZX_ASSERT(fdf_token != nullptr);
  ZX_ASSERT(dispatcher != nullptr);
  return dispatcher->ScheduleTokenCallback(fdf_token, ZX_OK, std::move(channel_));
}

}  // namespace driver_runtime
