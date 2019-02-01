// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <fuchsia/device/mock/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/fidl/coding.h>
#include <lib/fit/function.h>
#include <lib/fit/bridge.h>
#include <lib/zx/channel.h>

#include "action-list.h"

namespace libdriver_integration_test {

// Base class of the hook hierarchy.  It provides default implementations that
// will return errors if invoked.
class MockDeviceHooks : public fuchsia::device::mock::MockDevice {
public:
    using Completer = fit::completer<void, std::string>;
    explicit MockDeviceHooks(Completer completer);

    using HookInvocation = fuchsia::device::mock::HookInvocation;
    void Bind(HookInvocation record, BindCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Release(HookInvocation record) override {
        Fail(__FUNCTION__);
    }

    void GetProtocol(HookInvocation record, uint32_t protocol_id,
                         GetProtocolCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Open(HookInvocation record, uint32_t flags, OpenCallback callback) override {
        Fail(__FUNCTION__);
    }

    void OpenAt(HookInvocation record, std::string path, uint32_t flags,
                OpenAtCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Close(HookInvocation record, uint32_t flags, CloseCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Unbind(HookInvocation record, UnbindCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Read(HookInvocation record, uint64_t count, zx_off_t off,
                        ReadCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Write(HookInvocation record, std::vector<uint8_t> buffer, zx_off_t off,
                   WriteCallback callback) override {
        Fail(__FUNCTION__);
    }

    void GetSize(HookInvocation record, GetSizeCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Suspend(HookInvocation record, uint32_t flags, SuspendCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Resume(HookInvocation record, uint32_t flags, ResumeCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Ioctl(HookInvocation record, uint32_t op, std::vector<uint8_t> in,
                   uint64_t out_count, IoctlCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Message(HookInvocation record, MessageCallback callback) override {
        Fail(__FUNCTION__);
    }

    void Rxrpc(HookInvocation record, RxrpcCallback callback) override {
        Fail(__FUNCTION__);
    }

    void AddDeviceDone(uint64_t action_id) override {
        ZX_ASSERT(false);
    }
    void RemoveDeviceDone(uint64_t action_id) override {
        ZX_ASSERT(false);
    }

    virtual ~MockDeviceHooks() = default;

    void Fail(const char* function) {
        std::string message("Unexpected ");
        message.append(function);
        ADD_FAILURE() << message;
        if (completer_) {
            completer_.complete_error(std::move(message));
        }
    }

    void set_action_list_finalizer(fit::function<std::vector<ActionList::Action>(ActionList)>
                                   finalizer) {
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
            : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) { }
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
            : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) { }
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
            : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) { }
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
            : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) { }
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
            : MockDeviceHooks(std::move(completer)), callback_(std::move(callback)) { }
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

} // namespace libdriver_integration_test
