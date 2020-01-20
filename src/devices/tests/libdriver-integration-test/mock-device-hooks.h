// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/device/mock/cpp/fidl.h>
#include <lib/fidl/coding.h>
#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "action-list.h"

namespace libdriver_integration_test {

// Base class of the hook hierarchy.  It provides default implementations that
// will return errors if invoked.
class MockDeviceHooks : public fuchsia::device::mock::MockDevice {
 public:
  using Completer = fit::completer<void, std::string>;
  explicit MockDeviceHooks(Completer completer);

  using HookInvocation = fuchsia::device::mock::HookInvocation;
  void Bind(HookInvocation record, BindCallback callback) override { Fail(__FUNCTION__); }

  void Release(HookInvocation record) override { Fail(__FUNCTION__); }

  void GetProtocol(HookInvocation record, uint32_t protocol_id,
                   GetProtocolCallback callback) override {
    Fail(__FUNCTION__);
  }

  void Open(HookInvocation record, uint32_t flags, OpenCallback callback) override {
    Fail(__FUNCTION__);
  }

  void Close(HookInvocation record, uint32_t flags, CloseCallback callback) override {
    Fail(__FUNCTION__);
  }

  void Unbind(HookInvocation record, UnbindCallback callback) override { Fail(__FUNCTION__); }

  void Read(HookInvocation record, uint64_t count, zx_off_t off, ReadCallback callback) override {
    Fail(__FUNCTION__);
  }

  void Write(HookInvocation record, std::vector<uint8_t> buffer, zx_off_t off,
             WriteCallback callback) override {
    Fail(__FUNCTION__);
  }

  void GetSize(HookInvocation record, GetSizeCallback callback) override { Fail(__FUNCTION__); }

  void Suspend(HookInvocation record, uint8_t requested_state, bool enable_wake,
               uint8_t suspend_reason, SuspendCallback callback) override {
    Fail(__FUNCTION__);
  }

  void Resume(HookInvocation record, uint32_t flags, ResumeCallback callback) override {
    Fail(__FUNCTION__);
  }

  void Message(HookInvocation record, MessageCallback callback) override { Fail(__FUNCTION__); }

  void Rxrpc(HookInvocation record, RxrpcCallback callback) override { Fail(__FUNCTION__); }

  void AddDeviceDone(uint64_t action_id) final { ZX_ASSERT(false); }
  void UnbindReplyDone(uint64_t action_id) final { ZX_ASSERT(false); }
  void SuspendReplyDone(uint64_t action_id) final { ZX_ASSERT(false); }

  virtual ~MockDeviceHooks() = default;

  void Fail(const char* function) {
    std::string message("Unexpected ");
    message.append(function);
    ADD_FAILURE() << message;
    if (completer_) {
      completer_.complete_error(std::move(message));
    }
  }

  void set_action_list_finalizer(
      fit::function<std::vector<ActionList::Action>(ActionList)> finalizer) {
    action_list_finalizer_ = std::move(finalizer);
  }

 protected:
  Completer completer_;
  fit::function<std::vector<ActionList::Action>(ActionList)> action_list_finalizer_;
};

class BindOnce : public MockDeviceHooks {
 public:
  using Callback = fit::function<ActionList(HookInvocation, Completer)>;

  BindOnce(Completer completer, Callback callback)
      : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) {}
  virtual ~BindOnce() = default;

  void Bind(HookInvocation record, BindCallback callback) override {
    if (!completer_) {
      return Fail(__FUNCTION__);
    }
    callback(action_list_finalizer_(callback_(record, std::move(completer_))));
  }

 private:
  Callback callback_;
};

class UnbindOnce : public MockDeviceHooks {
 public:
  using Callback = fit::function<ActionList(HookInvocation, Completer)>;

  UnbindOnce(Completer completer, Callback callback)
      : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) {}
  virtual ~UnbindOnce() = default;

  void Unbind(HookInvocation record, UnbindCallback callback) override {
    if (!completer_) {
      return Fail(__FUNCTION__);
    }
    callback(action_list_finalizer_(callback_(record, std::move(completer_))));
  }

 private:
  Callback callback_;
};

class OpenOnce : public MockDeviceHooks {
 public:
  using Callback = fit::function<ActionList(HookInvocation, uint32_t, Completer)>;

  OpenOnce(Completer completer, Callback callback)
      : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) {}
  virtual ~OpenOnce() = default;

  void Open(HookInvocation record, uint32_t flags, OpenCallback callback) override {
    if (!completer_) {
      return Fail(__FUNCTION__);
    }
    callback(action_list_finalizer_(callback_(record, flags, std::move(completer_))));
  }

 private:
  Callback callback_;
};

class CloseOnce : public MockDeviceHooks {
 public:
  using Callback = fit::function<ActionList(HookInvocation, uint32_t, Completer)>;

  CloseOnce(Completer completer, Callback callback)
      : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) {}
  virtual ~CloseOnce() = default;

  void Close(HookInvocation record, uint32_t flags, CloseCallback callback) override {
    if (!completer_) {
      return Fail(__FUNCTION__);
    }
    callback(action_list_finalizer_(callback_(record, flags, std::move(completer_))));
  }

 private:
  Callback callback_;
};

class ReleaseOnce : public MockDeviceHooks {
 public:
  using Callback = fit::function<void(HookInvocation, Completer)>;

  ReleaseOnce(Completer completer, Callback callback)
      : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) {}
  virtual ~ReleaseOnce() = default;

  void Release(HookInvocation record) override {
    if (!completer_) {
      return Fail(__FUNCTION__);
    }
    callback_(record, std::move(completer_));
  }

 private:
  Callback callback_;
};

// Class for expecting a sequence of hooks in any order (each once)
class UnorderedHooks : public MockDeviceHooks {
 public:
  // Construct a set of hooks that will complete |completer| after they all
  // run.
  explicit UnorderedHooks(Completer completer) : MockDeviceHooks(std::move(completer)) {}
  virtual ~UnorderedHooks() = default;

  void Bind(HookInvocation record, BindCallback callback) override;
  void Release(HookInvocation record) override;
  void GetProtocol(HookInvocation record, uint32_t protocol_id,
                   GetProtocolCallback callback) override;
  void Open(HookInvocation record, uint32_t flags, OpenCallback callback) override;
  void Close(HookInvocation record, uint32_t flags, CloseCallback callback) override;
  void Unbind(HookInvocation record, UnbindCallback callback) override;
  void Read(HookInvocation record, uint64_t count, zx_off_t off, ReadCallback callback) override;
  void Write(HookInvocation record, std::vector<uint8_t> buffer, zx_off_t off,
             WriteCallback callback) override;
  void GetSize(HookInvocation record, GetSizeCallback callback) override;
  void Suspend(HookInvocation record, uint8_t requested_state, bool enable_wake,
               uint8_t suspend_reason, SuspendCallback callback) override;
  void Resume(HookInvocation record, uint32_t flags, ResumeCallback callback) override;
  void Message(HookInvocation record, MessageCallback callback) override;
  void Rxrpc(HookInvocation record, RxrpcCallback callback) override;

 private:
  // Check if all of the hooks have been run, and if so complete the
  // completer.
  void TryFinish();

  fit::function<ActionList(HookInvocation)> bind_;
  fit::function<void(HookInvocation)> release_;
  fit::function<ActionList(HookInvocation, uint32_t)> get_protocol_;
  fit::function<ActionList(HookInvocation, uint32_t)> open_;
  fit::function<ActionList(HookInvocation, std::string, uint32_t)> open_at_;
  fit::function<ActionList(HookInvocation, uint32_t)> close_;
  fit::function<ActionList(HookInvocation)> unbind_;
  fit::function<ActionList(HookInvocation, uint64_t, zx_off_t)> read_;
  fit::function<ActionList(HookInvocation, std::vector<uint8_t>, zx_off_t)> write_;
  fit::function<ActionList(HookInvocation)> get_size_;
  fit::function<ActionList(HookInvocation, uint8_t requested_state, bool enable_wake, uint8_t suspend_reason)> suspend_;
  fit::function<ActionList(HookInvocation, uint32_t)> resume_;
  fit::function<ActionList(HookInvocation)> message_;
  fit::function<ActionList(HookInvocation)> rxrpc_;

 public:
  void set_bind(decltype(bind_) hook) { bind_ = std::move(hook); }
  void set_release(decltype(release_) hook) { release_ = std::move(hook); }
  void set_get_protocol(decltype(get_protocol_) hook) { get_protocol_ = std::move(hook); }
  void set_open(decltype(open_) hook) { open_ = std::move(hook); }
  void set_open_at(decltype(open_at_) hook) { open_at_ = std::move(hook); }
  void set_close(decltype(close_) hook) { close_ = std::move(hook); }
  void set_unbind(decltype(unbind_) hook) { unbind_ = std::move(hook); }
  void set_read(decltype(read_) hook) { read_ = std::move(hook); }
  void set_write(decltype(write_) hook) { write_ = std::move(hook); }
  void set_get_size(decltype(get_size_) hook) { get_size_ = std::move(hook); }
  void set_suspend(decltype(suspend_) hook) { suspend_ = std::move(hook); }
  void set_resume(decltype(resume_) hook) { resume_ = std::move(hook); }
  void set_message(decltype(message_) hook) { message_ = std::move(hook); }
  void set_rxrpc(decltype(rxrpc_) hook) { rxrpc_ = std::move(hook); }
};

class IgnoreGetProtocol : public MockDeviceHooks {
 public:
  explicit IgnoreGetProtocol() : MockDeviceHooks({}) {}
  virtual ~IgnoreGetProtocol() = default;

  void GetProtocol(HookInvocation record, uint32_t proto, GetProtocolCallback callback) override {
    ActionList actions;
    actions.AppendReturnStatus(ZX_ERR_NOT_SUPPORTED);
    callback(action_list_finalizer_(std::move(actions)));
  }

 private:
};

}  // namespace libdriver_integration_test
