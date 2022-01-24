// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/test/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <type_traits>
#include <variant>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "ddktl/suspend-txn.h"
#include "fidl.h"
#include "src/devices/tests/mock-device/mock-device-bind.h"

namespace mock_device {

class MockDevice;
using MockDeviceType = ddk::FullDevice<MockDevice>;

class MockDevice : public MockDeviceType {
 public:
  MockDevice(zx_device_t* device, fidl::ClientEnd<device_mock::MockDevice> controller);
  static zx_status_t Create(zx_device_t* parent,
                            fidl::ClientEnd<device_mock::MockDevice> controller,
                            std::unique_ptr<MockDevice>* out);

  // Device protocol implementation.
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
  zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual);
  zx_off_t DdkGetSize();
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkResume(ddk::ResumeTxn txn);
  zx_status_t DdkRxrpc(zx_handle_t channel);

  // Generate an invocation record for a hook RPC
  device_mock::wire::HookInvocation ConstructHookInvocation();
  static device_mock::wire::HookInvocation ConstructHookInvocation(uint64_t device_id);

  // Create a new thread that will serve a MockDeviceThread interface over |server_end|
  void CreateThread(fidl::ServerEnd<device_mock::MockDeviceThread> server_end);

 private:
  // Retrieve the current thread's process and thread koids
  static void GetThreadKoids(zx_koid_t* process, zx_koid_t* thread);

  static int ThreadFunc(void* arg);
  struct ThreadFuncArg {
    // channel that the thread will use to serve a MockDeviceThread
    // interface over
    fidl::ServerEnd<device_mock::MockDeviceThread> server_end;
    // Device this thread is executing for
    MockDevice* dev;
  };

  fbl::Mutex lock_;
  // List of threads spawned by Actions
  fbl::Vector<thrd_t> threads_ TA_GUARDED(lock_);

  // Our half of the controller channel.  We will send requests for input on
  // it.
  fidl::WireSyncClient<device_mock::MockDevice> controller_;
};

struct ProcessActionsContext {
  // A channel that is either borrowing (zx::unowned_channel) or
  // owned (EventSender).
  // In the borrowing case, the channel must outlive this variant.
  using ChannelVariants =
      std::variant<zx::unowned_channel, fidl::WireEventSender<device_mock::MockDeviceThread>>;

  // Constructs a process action context. Note that |channel_variants| must
  // outlive this context. Typically, the |channel_variants| is first created
  // on the caller's stack, after which |ProcessActionContext| is constructed
  // referencing it.
  ProcessActionsContext(ChannelVariants& channel_variants, bool has_hook_status,
                        MockDevice* mock_device, zx_device_t* device)
      : channel_variants(channel_variants),
        has_hook_status(has_hook_status),
        mock_device(mock_device),
        device(device) {}

  // IN: The channel that these actions came from.  Used for acknowledging
  // add/remove device requests.
  //
  // When this context is running in a separate thread, the context has the
  // |fidl::WireEventSender<device_mock::MockDeviceThread>| variant i.e. it is the
  // server-end of the MockDeviceThread protocol.
  // When this context is running in the same thread, the context has the
  // |zx::unowned_channel| variant, and is the client-end of the
  // MockDevice protocol.
  //
  // Note that in either case, the context does not own the underlying channel,
  // since this field is a reference. The channel is usually owned by a caller
  // which created the context.
  ChannelVariants& channel_variants;

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

  // IN: The txn used to reply to the unbind hook.
  std::optional<ddk::UnbindTxn> pending_unbind_txn = std::nullopt;
  // IN: The txn used to reply to the suspend hook.
  std::optional<ddk::SuspendTxn> pending_suspend_txn = std::nullopt;
  // IN: The txn used to reply to the resume hook.
  std::optional<ddk::ResumeTxn> pending_resume_txn = std::nullopt;
};

// Helper struct to simplify matching an |std::variant|.
template <class... Ts>
struct matchers : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
matchers(Ts...) -> matchers<Ts...>;

// Execute the actions returned by a hook
zx_status_t ProcessActions(fidl::VectorView<device_mock::wire::Action> actions,
                           ProcessActionsContext* context);

MockDevice::MockDevice(zx_device_t* device, fidl::ClientEnd<device_mock::MockDevice> controller)
    : MockDeviceType(device),
      controller_(fidl::WireSyncClient<device_mock::MockDevice>(std::move(controller))) {}

int MockDevice::ThreadFunc(void* raw_arg) {
  auto arg = std::unique_ptr<ThreadFuncArg>(static_cast<ThreadFuncArg*>(raw_arg));
  std::variant<zx::unowned_channel, fidl::WireEventSender<device_mock::MockDeviceThread>>
      event_sender =
          fidl::WireEventSender<device_mock::MockDeviceThread>(std::move(arg->server_end));

  while (true) {
    fbl::Array<device_mock::wire::Action> actions;
    zx_status_t status = WaitForPerformActions(
        std::get<fidl::WireEventSender<device_mock::MockDeviceThread>>(event_sender).channel(),
        &actions);
    if (status != ZX_OK) {
      ZX_ASSERT_MSG(status == ZX_ERR_STOP, "MockDevice thread exiting: %s\n",
                    zx_status_get_string(status));
      break;
    }
    ProcessActionsContext ctx(event_sender, false, arg->dev, arg->dev->zxdev());
    status = ProcessActions(
        fidl::VectorView<device_mock::wire::Action>::FromExternal(actions.data(), actions.size()),
        &ctx);
    ZX_ASSERT(status == ZX_OK);
    if (ctx.device == nullptr) {
      // If the device was removed, bail out since we're releasing.
      break;
    }
  }
  return 0;
}

void MockDevice::CreateThread(fidl::ServerEnd<device_mock::MockDeviceThread> server_end) {
  auto arg = std::make_unique<ThreadFuncArg>();
  arg->server_end = std::move(server_end);
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

device_mock::wire::HookInvocation MockDevice::ConstructHookInvocation(uint64_t device_id) {
  device_mock::wire::HookInvocation invoc;
  GetThreadKoids(&invoc.process_koid, &invoc.thread_koid);
  invoc.device_id = device_id;
  return invoc;
}

device_mock::wire::HookInvocation MockDevice::ConstructHookInvocation() {
  return ConstructHookInvocation(reinterpret_cast<uintptr_t>(zxdev()));
}

void MockDevice::DdkRelease() {
  auto result = controller_->Release(ConstructHookInvocation());
  ZX_ASSERT(result.ok());

  // Launch a thread to do the actual joining and delete, since this could get
  // called from a thread.
  thrd_t thrd;
  int ret = thrd_create(
      &thrd,
      [](void* arg) {
        auto me = static_cast<MockDevice*>(arg);
        {
          fbl::AutoLock guard(&me->lock_);
          for (auto& t : me->threads_) {
            thrd_join(t, nullptr);
          }
        }
        delete me;
        return 0;
      },
      this);
  ZX_ASSERT(ret == thrd_success);
  thrd_detach(thrd);
}

zx_status_t MockDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
  auto result = controller_->GetProtocol(ConstructHookInvocation(), proto_id);
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  return ctx.hook_status;
}

zx_status_t MockDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  auto result = controller_->Open(ConstructHookInvocation(), flags);
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  return ctx.hook_status;
}

zx_status_t MockDevice::DdkClose(uint32_t flags) {
  auto result = controller_->Close(ConstructHookInvocation(), flags);
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  return ctx.hook_status;
}

void MockDevice::DdkUnbind(ddk::UnbindTxn txn) {
  auto result = controller_->Unbind(ConstructHookInvocation());
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, false, this, zxdev());
  ctx.pending_unbind_txn = std::move(txn);
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
}

zx_status_t MockDevice::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
  auto result = controller_->Read(ConstructHookInvocation(), count, off);
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  ctx.associated_buf = buf, ctx.associated_buf_count = count;
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  *actual = ctx.associated_buf_actual;
  return ctx.hook_status;
}

zx_status_t MockDevice::DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) {
  auto result = controller_->Write(
      ConstructHookInvocation(),
      fidl::VectorView<uint8_t>::FromExternal(static_cast<uint8_t*>(const_cast<void*>(buf)), count),
      off);
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  *actual = count;
  return ctx.hook_status;
}

zx_off_t MockDevice::DdkGetSize() {
  auto result = controller_->GetSize(ConstructHookInvocation());
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, false, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);

  ZX_ASSERT_MSG(false, "need to plumb returning values in\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  auto result = controller_->Message(ConstructHookInvocation());
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  return ctx.hook_status;
}

void MockDevice::DdkSuspend(ddk::SuspendTxn txn) {
  auto result = controller_->Suspend(ConstructHookInvocation(), txn.requested_state(),
                                     txn.enable_wake(), txn.suspend_reason());
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  ctx.pending_suspend_txn = std::move(txn);
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
}

void MockDevice::DdkResume(ddk::ResumeTxn txn) {
  auto result = controller_->Resume(ConstructHookInvocation(), txn.requested_state());
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel = controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel, true, this, zxdev());
  ctx.pending_resume_txn = std::move(txn);
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
}

zx_status_t MockDevice::DdkRxrpc(zx_handle_t channel) {
  auto result = controller_->Rxrpc(ConstructHookInvocation());
  ZX_ASSERT(result.ok());
  ProcessActionsContext::ChannelVariants channel_variants =
      controller_.client_end().borrow().channel();
  ProcessActionsContext ctx(channel_variants, true, this, zxdev());
  zx_status_t status = ProcessActions(std::move(result->actions), &ctx);
  ZX_ASSERT(status == ZX_OK);
  return ctx.hook_status;
}

zx_status_t MockDevice::Create(zx_device_t* parent,
                               fidl::ClientEnd<device_mock::MockDevice> controller,
                               std::unique_ptr<MockDevice>* out) {
  auto dev = std::make_unique<MockDevice>(parent, std::move(controller));
  *out = std::move(dev);
  return ZX_OK;
}

zx_status_t ProcessActions(fidl::VectorView<device_mock::wire::Action> actions,
                           ProcessActionsContext* ctx) {
  for (size_t i = 0; i < actions.count(); ++i) {
    auto& action = actions[i];
    switch (action.Which()) {
      case device_mock::wire::Action::Tag::kReturnStatus: {
        if (i != actions.count() - 1) {
          printf("MockDevice::ProcessActions: return_status was not the final entry\n");
          return ZX_ERR_INVALID_ARGS;
        }
        if (!ctx->has_hook_status) {
          printf("MockDevice::ProcessActions: return_status present for no-status hook\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->hook_status = action.return_status();
        return ZX_OK;
      }
      case device_mock::wire::Action::Tag::kWrite: {
        if (ctx->associated_buf == nullptr) {
          printf("MockDevice::ProcessActions: write action with no associated buf\n");
          return ZX_ERR_INVALID_ARGS;
        }
        auto& write_action = action.write();
        if (write_action.count() > ctx->associated_buf_count) {
          printf("MockDevice::ProcessActions: write action too large\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->associated_buf_actual = write_action.count();
        memcpy(ctx->associated_buf, write_action.data(), write_action.count());
        break;
      }
      case device_mock::wire::Action::Tag::kCreateThread: {
        if (ctx->mock_device == nullptr) {
          printf("MockDevice::CreateThread: asked to create thread without device\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->mock_device->CreateThread(std::move(action.mutable_create_thread()));
        break;
      }
      case device_mock::wire::Action::Tag::kAsyncRemoveDevice: {
        if (ctx->mock_device == nullptr) {
          printf("MockDevice::RemoveDevice: asked to remove device but none populated\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->mock_device->DdkAsyncRemove();
        break;
      }
      case device_mock::wire::Action::Tag::kUnbindReply: {
        if (!ctx->pending_unbind_txn) {
          printf("MockDevice::UnbindReply: asked to reply to unbind but no unbind is pending\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->pending_unbind_txn->Reply();
        // Null out the device pointers, since the release hook might get
        // activated.
        ctx->device = nullptr;
        ctx->mock_device = nullptr;
        zx_status_t status = std::visit(
            matchers{
                [&](fidl::WireEventSender<device_mock::MockDeviceThread>& sender) {
                  return sender.UnbindReplyDone(action.unbind_reply().action_id).status();
                },
                [&](zx::unowned_channel& channel) {
                  return fidl::WireCall<device_mock::MockDevice>(zx::unowned_channel(channel))
                      ->UnbindReplyDone(action.unbind_reply().action_id)
                      .status();
                },
            },
            ctx->channel_variants);
        ZX_ASSERT(status == ZX_OK);
        break;
      }

      case device_mock::wire::Action::Tag::kSuspendReply: {
        if (!ctx->pending_suspend_txn) {
          printf("MockDevice::SuspendReply: asked to reply to suspend but no suspend is pending\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->pending_suspend_txn->Reply(ZX_OK, 0);
        zx_status_t status = std::visit(
            matchers{
                [&](fidl::WireEventSender<device_mock::MockDeviceThread>& sender) {
                  return sender.SuspendReplyDone(action.suspend_reply().action_id).status();
                },
                [&](zx::unowned_channel& channel) {
                  return fidl::WireCall<device_mock::MockDevice>(zx::unowned_channel(channel))
                      ->SuspendReplyDone(action.suspend_reply().action_id)
                      .status();
                },
            },
            ctx->channel_variants);
        ZX_ASSERT(status == ZX_OK);
        break;
      }

      case device_mock::wire::Action::Tag::kResumeReply: {
        if (!ctx->pending_resume_txn) {
          printf("MockDevice::ResumeReply: asked to reply to resume but no resume is pending\n");
          return ZX_ERR_INVALID_ARGS;
        }
        ctx->pending_resume_txn->Reply(ZX_OK, 0, 0);
        zx_status_t status = std::visit(
            matchers{
                [&](fidl::WireEventSender<device_mock::MockDeviceThread>& sender) {
                  return sender.ResumeReplyDone(action.resume_reply().action_id).status();
                },
                [&](zx::unowned_channel& channel) {
                  return fidl::WireCall<device_mock::MockDevice>(zx::unowned_channel(channel))
                      ->ResumeReplyDone(action.resume_reply().action_id)
                      .status();
                },
            },
            ctx->channel_variants);
        ZX_ASSERT(status == ZX_OK);
        break;
      }
      case device_mock::wire::Action::Tag::kAddDevice: {
        // TODO(teisenbe): Implement more functionality here
        auto& add_device_action = action.mutable_add_device();
        ZX_ASSERT_MSG(!add_device_action.do_bind, "bind not yet supported\n");
        std::unique_ptr<MockDevice> dev;
        zx_status_t status =
            MockDevice::Create(ctx->device, std::move(add_device_action.controller), &dev);
        if (status != ZX_OK) {
          return status;
        }

        char name[ZX_DEVICE_NAME_MAX + 1];
        if (add_device_action.name.size() > sizeof(name) - 1) {
          return ZX_ERR_INVALID_ARGS;
        }
        memcpy(name, add_device_action.name.data(), add_device_action.name.size());
        name[add_device_action.name.size()] = 0;

        cpp20::span<zx_device_prop_t> props(
            reinterpret_cast<zx_device_prop_t*>(add_device_action.properties.mutable_data()),
            add_device_action.properties.count());

        status = dev->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
        if (status == ZX_OK) {
          // Devmgr now owns this
          __UNUSED auto ptr = dev.release();
        }
        if (add_device_action.expect_status != status) {
          return status;
        }

        status = std::visit(
            matchers{
                [&](fidl::WireEventSender<device_mock::MockDeviceThread>& sender) {
                  return sender.AddDeviceDone(add_device_action.action_id).status();
                },
                [&](zx::unowned_channel& channel) {
                  return fidl::WireCall<device_mock::MockDevice>(zx::unowned_channel(channel))
                      ->AddDeviceDone(add_device_action.action_id)
                      .status();
                },
            },
            ctx->channel_variants);
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
  auto result =
      fidl::WireCall<device_mock::MockDevice>(zx::unowned_channel(c))
          ->Bind(MockDevice::ConstructHookInvocation(reinterpret_cast<uintptr_t>(parent)));
  ZX_ASSERT(result.ok());

  ProcessActionsContext::ChannelVariants channel_variants = zx::unowned_channel(c);
  ProcessActionsContext pac_ctx(channel_variants, true, nullptr, parent);
  status = ProcessActions(std::move(result->actions), &pac_ctx);
  ZX_ASSERT(status == ZX_OK);
  return pac_ctx.hook_status;
}

const zx_driver_ops_t kMockDeviceOps = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = MockDeviceBind;
  return ops;
}();

}  // namespace mock_device

ZIRCON_DRIVER(mock_device, mock_device::kMockDeviceOps, "zircon", "0.1");
