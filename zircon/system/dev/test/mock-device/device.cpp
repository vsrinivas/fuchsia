// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "fidl.h"

namespace mock_device {

class MockDevice;
using MockDeviceType = ddk::FullDevice<MockDevice>;

class MockDevice : public MockDeviceType {
public:
    MockDevice(zx_device_t* device, zx::channel controller);
    static zx_status_t Create(zx_device_t* parent, zx::channel controller,
                              fbl::unique_ptr<MockDevice>* out);

    // Device protocol implementation.
    void DdkRelease();
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    zx_status_t DdkClose(uint32_t flags);
    void DdkUnbind();
    zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
    zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual);
    zx_off_t DdkGetSize();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* actual);
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    zx_status_t DdkSuspend(uint32_t flags);
    zx_status_t DdkResume(uint32_t flags);
    zx_status_t DdkRxrpc(zx_handle_t channel);

    // Generate an invocation record for a hook RPC
    fuchsia_device_mock_HookInvocation ConstructHookInvocation();
    static fuchsia_device_mock_HookInvocation ConstructHookInvocation(uint64_t device_id);

    // Create a new thread that will serve a MockDeviceThread interface over |c|
    void CreateThread(zx::channel channel);
private:

    // Retrieve the current thread's process and thread koids
    static void GetThreadKoids(zx_koid_t* process, zx_koid_t* thread);

    static int ThreadFunc(void* arg);
    struct ThreadFuncArg {
        // channel that the thread will use to serve a MockDeviceThread
        // interface over
        zx::channel channel;
        // Device this thread is executing for
        MockDevice* dev;
    };

    fbl::Mutex lock_;
    // List of threads spawned by Actions
    fbl::Vector<thrd_t> threads_ TA_GUARDED(lock_);

    // Our half of the controller channel.  We will send requests for input on
    // it.
    zx::channel controller_;
};

struct ProcessActionsContext {
    ProcessActionsContext(const zx::channel& channel, bool has_hook_status, MockDevice* mock_device,
                          zx_device_t* device)
        : channel(zx::unowned_channel(channel)), has_hook_status(has_hook_status),
          mock_device(mock_device), device(device) {
    }

    // IN: The channel that these actions came from.  Used for acknowledging
    // add/remove device requests.
    zx::unowned_channel channel;

    bool has_hook_status = false;
    // OUT: What should be returned by the hook
    zx_status_t hook_status = ZX_ERR_INTERNAL;
    // IN: A buffer that can be written to by actions (nullptr if none)
    void* associated_buf = nullptr;
    size_t associated_buf_count = 0;
    // OUT: Number of bytes written by actions
    size_t associated_buf_actual = 0;
    // IN/OUT: MockDevice to use for associating threads with.  NULL'd out if
    // remove was called.
    MockDevice* mock_device = nullptr;
    // IN/OUT: Device to use for invoking add_device/remove_device.  NULL'd out
    // if remove was called.
    zx_device_t* device = nullptr;
    // IN: Whether or not this context is running in a separate thread
    bool is_thread = false;
};

// Execute the actions returned by a hook
zx_status_t ProcessActions(fbl::Array<const fuchsia_device_mock_Action> actions,
                           ProcessActionsContext* context);

MockDevice::MockDevice(zx_device_t* device, zx::channel controller)
    : MockDeviceType(device), controller_(std::move(controller)) {
}

int MockDevice::ThreadFunc(void* raw_arg) {
    auto arg = fbl::unique_ptr<ThreadFuncArg>(static_cast<ThreadFuncArg*>(raw_arg));

    while (true) {
        fbl::Array<const fuchsia_device_mock_Action> actions;
        zx_status_t status = WaitForPerformActions(arg->channel, &actions);
        if (status != ZX_OK) {
            ZX_ASSERT_MSG(status == ZX_ERR_STOP, "MockDevice thread exiting: %s\n",
                          zx_status_get_string(status));
            break;
        }
        ProcessActionsContext ctx(arg->channel, false, arg->dev, arg->dev->zxdev());
        ctx.is_thread = true;
        status = ProcessActions(std::move(actions), &ctx);
        ZX_ASSERT(status == ZX_OK);
        if (ctx.device == nullptr) {
            // If the device was removed, bail out since we're releasing.
            break;
        }
    }
    return 0;
}

void MockDevice::CreateThread(zx::channel channel) {
    auto arg = std::make_unique<ThreadFuncArg>();
    arg->channel = std::move(channel);
    arg->dev = this;

    fbl::AutoLock guard(&lock_);
    thrd_t thrd;
    int ret = thrd_create(&thrd, MockDevice::ThreadFunc, arg.get());
    ZX_ASSERT(ret == thrd_success);
    // The thread now owns this pointer
    __UNUSED auto ptr = arg.release();

    threads_.push_back(thrd);
}

void MockDevice::GetThreadKoids(zx_koid_t* process, zx_koid_t* thread) {
    struct Koids {
        zx_koid_t process;
        zx_koid_t thread;
    };
    thread_local Koids thread_koids;

    if (thread_koids.process == 0 && thread_koids.thread == 0) {
        zx_info_handle_basic_t info;
        zx_status_t status = zx_object_get_info(zx_thread_self(), ZX_INFO_HANDLE_BASIC, &info,
                                                sizeof(info), nullptr, nullptr);
        ZX_ASSERT(status == ZX_OK);
        thread_koids.process = info.related_koid;
        thread_koids.thread = info.koid;
    }

    *process = thread_koids.process;
    *thread = thread_koids.thread;
}

fuchsia_device_mock_HookInvocation MockDevice::ConstructHookInvocation(uint64_t device_id) {
    fuchsia_device_mock_HookInvocation invoc;
    GetThreadKoids(&invoc.process_koid, &invoc.thread_koid);
    invoc.device_id = device_id;
    return invoc;
}

fuchsia_device_mock_HookInvocation MockDevice::ConstructHookInvocation() {
    return ConstructHookInvocation(reinterpret_cast<uintptr_t>(zxdev()));
}

void MockDevice::DdkRelease() {
    zx_status_t status = ReleaseHook(controller_, ConstructHookInvocation());
    ZX_ASSERT(status == ZX_OK);

    // Launch a thread to do the actual joining and delete, since this could get
    // called from a thread.
    thrd_t thrd;
    int ret = thrd_create(&thrd, [](void* arg) {
        auto me = static_cast<MockDevice*>(arg);
        fbl::AutoLock guard(&me->lock_);
        for (auto& t : me->threads_) {
            thrd_join(t, nullptr);
        }
        delete me;
        return 0;
    }, this);
    ZX_ASSERT(ret == thrd_success);
    thrd_detach(thrd);
}

zx_status_t MockDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = GetProtocolHook(controller_, ConstructHookInvocation(),
                                         proto_id, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = OpenHook(controller_, ConstructHookInvocation(), flags, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkClose(uint32_t flags) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = CloseHook(controller_, ConstructHookInvocation(), flags, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

void MockDevice::DdkUnbind() {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = UnbindHook(controller_, ConstructHookInvocation(), &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, false, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
}

zx_status_t MockDevice::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = ReadHook(controller_, ConstructHookInvocation(), count, off, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    ctx.associated_buf = buf,
    ctx.associated_buf_count = count,
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    *actual = ctx.associated_buf_actual;
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = WriteHook(controller_, ConstructHookInvocation(),
                                   static_cast<const uint8_t*>(buf), count, off, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    *actual = count;
    return ctx.hook_status;
}

zx_off_t MockDevice::DdkGetSize() {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = GetSizeHook(controller_, ConstructHookInvocation(), &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, false, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);

    ZX_ASSERT_MSG(false, "need to plumb returning values in\n");
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockDevice::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                                 size_t out_len, size_t* actual) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = IoctlHook(controller_, ConstructHookInvocation(), op,
                                   static_cast<const uint8_t*>(in_buf), in_len,
                                   out_len, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    ctx.associated_buf = out_buf;
    ctx.associated_buf_count = out_len;
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    *actual = ctx.associated_buf_actual;
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = MessageHook(controller_, ConstructHookInvocation(), &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkSuspend(uint32_t flags) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = SuspendHook(controller_, ConstructHookInvocation(), flags, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkResume(uint32_t flags) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = ResumeHook(controller_, ConstructHookInvocation(), flags, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

zx_status_t MockDevice::DdkRxrpc(zx_handle_t channel) {
    fbl::Array<const fuchsia_device_mock_Action> actions;
    zx_status_t status = RxrpcHook(controller_, ConstructHookInvocation(), &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext ctx(controller_, true, this, zxdev());
    status = ProcessActions(std::move(actions), &ctx);
    ZX_ASSERT(status == ZX_OK);
    return ctx.hook_status;
}

zx_status_t MockDevice::Create(zx_device_t* parent, zx::channel controller,
                               fbl::unique_ptr<MockDevice>* out) {
    auto dev = std::make_unique<MockDevice>(parent, std::move(controller));
    *out = std::move(dev);
    return ZX_OK;
}

zx_status_t ProcessActions(fbl::Array<const fuchsia_device_mock_Action> actions,
                           ProcessActionsContext* ctx) {
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& action = actions[i];
        switch (action.tag) {
        case fuchsia_device_mock_ActionTag_return_status: {
            if (i != actions.size() - 1) {
                printf("MockDevice::ProcessActions: return_status was not the final entry\n");
                return ZX_ERR_INVALID_ARGS;
            }
            if (!ctx->has_hook_status) {
                printf("MockDevice::ProcessActions: return_status present for no-status hook\n");
                return ZX_ERR_INVALID_ARGS;
            }
            ctx->hook_status = action.return_status;
            return ZX_OK;
        }
        case fuchsia_device_mock_ActionTag_write: {
            if (ctx->associated_buf == nullptr) {
                printf("MockDevice::ProcessActions: write action with no associated buf\n");
                return ZX_ERR_INVALID_ARGS;
            }
            if (action.write.count > ctx->associated_buf_count) {
                printf("MockDevice::ProcessActions: write action too large\n");
                return ZX_ERR_INVALID_ARGS;
            }
            ctx->associated_buf_actual = action.write.count;
            memcpy(ctx->associated_buf, action.write.data, action.write.count);
            break;
        }
        case fuchsia_device_mock_ActionTag_create_thread: {
            if (ctx->mock_device == nullptr) {
                printf("MockDevice::CreateThread: asked to create thread without device\n");
                return ZX_ERR_INVALID_ARGS;
            }
            zx::channel thread_channel(action.create_thread);
            ctx->mock_device->CreateThread(std::move(thread_channel));
            break;
        }
        case fuchsia_device_mock_ActionTag_remove_device: {
            device_remove(ctx->device);
            // Null out the device pointers, since the release hook might get
            // activated.
            ctx->device = nullptr;
            ctx->mock_device = nullptr;
            zx_status_t status;
            if (ctx->is_thread) {
                status = SendRemoveDeviceDoneFromThread(*ctx->channel,
                                                        action.remove_device.action_id);
            } else {
                status = SendRemoveDeviceDone(*ctx->channel, action.remove_device.action_id);
            }
            ZX_ASSERT(status == ZX_OK);
            break;
        }
        case fuchsia_device_mock_ActionTag_add_device: {
            // TODO(teisenbe): Implement more functionality here
            ZX_ASSERT_MSG(!action.add_device.do_bind, "bind not yet supported\n");
            fbl::unique_ptr<MockDevice> dev;
            zx_status_t status = MockDevice::Create(ctx->device,
                                                    zx::channel(action.add_device.controller),
                                                    &dev);
            if (status != ZX_OK) {
                return status;
            }

            char name[ZX_DEVICE_NAME_MAX + 1];
            if (action.add_device.name.size > sizeof(name) - 1) {
                return ZX_ERR_INVALID_ARGS;
            }
            memcpy(name, action.add_device.name.data, action.add_device.name.size);
            name[action.add_device.name.size] = 0;

            status = dev->DdkAdd(name, DEVICE_ADD_NON_BINDABLE,
                                 static_cast<zx_device_prop_t*>(action.add_device.properties.data),
                                 static_cast<uint32_t>(action.add_device.properties.count));
            if (status == ZX_OK) {
                // Devmgr now owns this
                __UNUSED auto ptr = dev.release();
            }
            if (action.add_device.expect_status != status) {
                return status;
            }

            if (ctx->is_thread) {
                status = SendAddDeviceDoneFromThread(*ctx->channel, action.add_device.action_id);
            } else {
                status = SendAddDeviceDone(*ctx->channel, action.add_device.action_id);
            }
            ZX_ASSERT(status == ZX_OK);
            break;
        }
        }
    }

    if (!ctx->has_hook_status) {
        return ZX_OK;
    }

    printf("MockDevice::ProcessActions: return_status was not the final entry\n");
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t MockDeviceBind(void* ctx, zx_device_t* parent) {
    // It's expected that this driver is binding against a device created by the
    // fuchsia.device.test interface.  Get the protocol from the device we're
    // binding to so we can wire up the control channel.
    test_protocol_t proto;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_TEST, &proto);
    ZX_ASSERT(status == ZX_OK);

    zx::channel c;
    test_get_channel(&proto, c.reset_and_get_address());

    // Ask the control channel what to do about this bind().
    fbl::Array<const fuchsia_device_mock_Action> actions;
    auto invoc = MockDevice::ConstructHookInvocation(reinterpret_cast<uintptr_t>(parent));
    status = BindHook(c, invoc, &actions);
    ZX_ASSERT(status == ZX_OK);
    ProcessActionsContext pac_ctx(c, true, nullptr, parent);
    status = ProcessActions(std::move(actions), &pac_ctx);
    ZX_ASSERT(status == ZX_OK);
    return pac_ctx.hook_status;
}

const zx_driver_ops_t kMockDeviceOps = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = MockDeviceBind;
    return ops;
}();

} // namespace mock_device

ZIRCON_DRIVER_BEGIN(mock_device, mock_device::kMockDeviceOps, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_TEST),
ZIRCON_DRIVER_END(test_sysdev)
