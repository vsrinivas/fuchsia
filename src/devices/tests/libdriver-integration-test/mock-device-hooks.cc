// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-device-hooks.h"

namespace libdriver_integration_test {

MockDeviceHooks::MockDeviceHooks(Completer completer) : completer_(std::move(completer)) {}

void UnorderedHooks::Bind(HookInvocation record, BindCallback callback) {
  if (!bind_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(bind_(record)));
  bind_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Release(HookInvocation record) {
  if (!release_) {
    return Fail(__FUNCTION__);
  }
  release_(record);
  release_ = nullptr;
  TryFinish();
}

void UnorderedHooks::GetProtocol(HookInvocation record, uint32_t protocol_id,
                                 GetProtocolCallback callback) {
  if (!get_protocol_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(get_protocol_(record, protocol_id)));
  get_protocol_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Open(HookInvocation record, uint32_t flags, OpenCallback callback) {
  if (!open_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(open_(record, flags)));
  open_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Close(HookInvocation record, uint32_t flags, CloseCallback callback) {
  if (!close_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(close_(record, flags)));
  close_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Unbind(HookInvocation record, UnbindCallback callback) {
  if (!unbind_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(unbind_(record)));
  unbind_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Read(HookInvocation record, uint64_t count, zx_off_t off,
                          ReadCallback callback) {
  if (!read_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(read_(record, count, off)));
  read_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Write(HookInvocation record, std::vector<uint8_t> buffer, zx_off_t off,
                           WriteCallback callback) {
  if (!write_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(write_(record, std::move(buffer), off)));
  write_ = nullptr;
  TryFinish();
}

void UnorderedHooks::GetSize(HookInvocation record, GetSizeCallback callback) {
  if (!get_size_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(get_size_(record)));
  get_size_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Suspend(HookInvocation record, uint8_t requested_state, bool enable_wake,
                             uint8_t suspend_reason, SuspendCallback callback) {
  if (!suspend_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(suspend_(record, requested_state, enable_wake, suspend_reason)));
  suspend_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Resume(HookInvocation record, uint32_t requested_state,
                            ResumeCallback callback) {
  if (!resume_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(resume_(record, requested_state)));
  resume_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Message(HookInvocation record, MessageCallback callback) {
  if (!message_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(message_(record)));
  message_ = nullptr;
  TryFinish();
}

void UnorderedHooks::Rxrpc(HookInvocation record, RxrpcCallback callback) {
  if (!rxrpc_) {
    return Fail(__FUNCTION__);
  }
  callback(action_list_finalizer_(rxrpc_(record)));
  rxrpc_ = nullptr;
  TryFinish();
}

void UnorderedHooks::TryFinish() {
  if (bind_ || release_ || get_protocol_ || open_ || open_at_ || close_ || unbind_ || read_ ||
      write_ || get_size_ || suspend_ || resume_ || message_ || rxrpc_) {
    return;
  }

  completer_.complete_ok();
}

}  // namespace libdriver_integration_test
