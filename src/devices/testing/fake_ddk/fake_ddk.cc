// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <stdarg.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/syscalls/log.h>
#include <zircon/types.h>

#include <utility>

#include <zxtest/zxtest.h>

namespace fake_ddk {

zx_device_t* kFakeDevice = reinterpret_cast<zx_device_t*>(0x55);
zx_device_t* kFakeParent = reinterpret_cast<zx_device_t*>(0xaa);
fx_log_severity_t kMinLogSeverity = FX_LOG_INFO;
size_t kFakeFWSize = 0x1000;

zx_device_t* FakeDevice() {
  ZX_ASSERT_MSG(Bind::Instance() != nullptr,
                "Attemping to access FakeDevice before fake_ddk::Bind instance was initialized. "
                "Double check initialization ordering!");
  return kFakeDevice;
}

zx_device_t* FakeParent() {
  ZX_ASSERT_MSG(Bind::Instance() != nullptr,
                "Attemping to access FakeParent before fake_ddk::Bind instance was initialized. "
                "Double check initialization ordering!");
  return kFakeParent;
}

Bind* Bind::instance_ = nullptr;

Bind::Bind() {
  ZX_ASSERT(!instance_);
  instance_ = this;
}

Bind::~Bind() {
  JoinUnbindThread();
  instance_ = nullptr;
}

void Bind::JoinUnbindThread() {
  // Make sure the unbind hook has returned. Users might not have called |WaitUntilRemove| if they
  // reply to unbind from the same thread.
  if (unbind_op_ && unbind_started_ && !unbind_thread_joined_) {
    thrd_join(unbind_thread_, NULL);
    unbind_thread_joined_ = true;
  }
}

bool Bind::Ok() {
  JoinUnbindThread();
  EXPECT_TRUE(add_called_);
  EXPECT_EQ(has_init_hook_, init_reply_.has_value());
  // We do not check the actual value of |init_reply_|, as the test may wish to test
  // scenarios where the init failure is handled.
  EXPECT_TRUE(remove_called_);
  EXPECT_FALSE(bad_parent_);
  EXPECT_FALSE(bad_device_);
  // TODO(fxbug.dev/34344): Remove and make void once all dependent tests migrate to zxtest.
  return !zxtest::Runner::GetInstance()->CurrentTestHasFailures();
}

zx_status_t Bind::WaitUntilInitComplete() {
  return sync_completion_wait_deadline(&init_replied_sync_, zx::time::infinite().get());
}

zx_status_t Bind::WaitUntilRemove() {
  return sync_completion_wait_deadline(&remove_called_sync_, zx::time::infinite().get());
}

zx_status_t Bind::WaitUntilSuspend() {
  return sync_completion_wait_deadline(&suspend_called_sync_, zx::time::infinite().get());
}

void Bind::ExpectMetadata(const void* data, size_t data_length) {
  metadata_ = data;
  metadata_length_ = data_length;
}

void Bind::GetMetadataInfo(int* num_calls, size_t* length) {
  *num_calls = add_metadata_calls_;
  *length = metadata_length_;
}

void Bind::SetProtocols(fbl::Array<ProtocolEntry>&& protocols) {
  protocols_ = std::move(protocols);
}

void Bind::SetSize(zx_off_t size) { size_ = size; }

void Bind::SetMetadata(const void* data, size_t data_length) {
  get_metadata_ = data;
  get_metadata_length_ = data_length;
}

void Bind::StartUnbindIfNeeded() {
  // We need to call unbind from a separate thread, as some drivers may call DdkAsyncRemove
  // from a worker thread that they then try to join with in their unbind hook.
  // This will only be run once.
  auto unbind_thread = [](void* arg) -> int {
    auto bind = reinterpret_cast<Bind*>(arg);
    bind->unbind_op_(bind->op_ctx_);
    return 0;
  };

  // Only call the unbind hook once.
  if (unbind_op_ && !unbind_started_) {
    unbind_started_ = true;
    int res = thrd_create_with_name(&unbind_thread_, unbind_thread, this, "fake-ddk-unbind-thread");
    ZX_ASSERT(res == thrd_success);
  } else if (!unbind_op_) {
    // The unbind hook is optional. If not present, we should mark the device as removed.
    remove_called_ = true;
  }
}

zx_status_t Bind::DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                            zx_device_t** out) {
  if (args) {
    zx_handle_close(args->inspect_vmo);
  }
  zx_status_t status;
  if (parent != kFakeParent) {
    bad_parent_ = true;
  }

  if (args && args->ops) {
    if (args->ops->init) {
      has_init_hook_ = true;
    }
    if (args->ops->message) {
      std::optional<zx::channel> remote_channel = std::nullopt;
      if (args->client_remote) {
        remote_channel.emplace(args->client_remote);
      }

      if ((status = fidl_.SetMessageOp(args->ctx, args->ops->message, std::move(remote_channel))) <
          0) {
        return status;
      }
    }
    if (args->ops->unbind) {
      unbind_op_ = args->ops->unbind;
      op_ctx_ = args->ctx;
    }
  }

  *out = kFakeDevice;
  add_called_ = true;
  // This needs to come after setting |out|, as this sets the device's internal |zxdev_|,
  // which needs to be present for the InitTxn.
  if (has_init_hook_) {
    args->ops->init(args->ctx);
  }
  return ZX_OK;
}

void Bind::DeviceInitReply(zx_device_t* device, zx_status_t status,
                           const device_init_reply_args_t* args) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  init_reply_ = status;

  if (init_reply_ != ZX_OK) {
    // If the init fails, the device should be automatically unbound and removed.
    unbind_requested_ = true;
  }
  if (unbind_requested_) {
    StartUnbindIfNeeded();
  }
  sync_completion_signal(&init_replied_sync_);
}

zx_status_t Bind::DeviceRemove(zx_device_t* device) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  remove_called_ = true;
  sync_completion_signal(&remove_called_sync_);
  return ZX_OK;
}

void Bind::DeviceUnbindReply(zx_device_t* device) { DeviceRemove(device); }

void Bind::DeviceAsyncRemove(zx_device_t* device) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }

  unbind_requested_ = true;

  // We should not call unbind until the init hook has been replied to.
  if (!has_init_hook_ || init_reply_.has_value()) {
    StartUnbindIfNeeded();
  }
}

zx_status_t Bind::DeviceAddMetadata(zx_device_t* device, uint32_t type, const void* data,
                                    size_t length) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }

  if (metadata_) {
    if (length != metadata_length_ || memcmp(data, metadata_, length) != 0) {
      fprintf(stderr, "Unexpected metadata\n");
      return ZX_ERR_BAD_STATE;
    }
  } else {
    metadata_length_ += length;
  }
  add_metadata_calls_++;
  return ZX_OK;
}

zx_status_t Bind::DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* buf, size_t buflen,
                                    size_t* actual) {
  if (get_metadata_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  *actual = get_metadata_length_;
  if (buflen < get_metadata_length_) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(buf, get_metadata_, get_metadata_length_);
  get_metadata_calls_++;
  return ZX_OK;
}

zx_status_t Bind::DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size) {
  if (get_metadata_ == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  *out_size = get_metadata_length_;
  return ZX_OK;
}

void Bind::DeviceMakeVisible(zx_device_t* device) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  make_visible_called_ = true;
  return;
}

void Bind::DeviceSuspendComplete(zx_device_t* device, zx_status_t status, uint8_t out_state) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  sync_completion_signal(&suspend_called_sync_);
  return;
}

void Bind::DeviceResumeComplete(zx_device_t* device, zx_status_t status, uint8_t out_power_state,
                                uint32_t out_perf_state) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  resume_complete_called_ = true;
  return;
}

zx_status_t Bind::DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
  if (device != kFakeParent) {
    bad_device_ = true;
    return ZX_ERR_NOT_SUPPORTED;
  }
  auto out = reinterpret_cast<Protocol*>(protocol);
  for (const auto& proto : protocols_) {
    if (proto_id == proto.id) {
      out->ops = proto.proto.ops;
      out->ctx = proto.proto.ctx;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Bind::DeviceOpenProtocolSessionMultibindable(const zx_device_t* device,
                                                         uint32_t proto_id, void* protocol) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  device_open_protocol_session_multibindable_ = true;
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Bind::DeviceRebind(zx_device_t* device) {
  if (device != kFakeDevice) {
    bad_device_ = true;
  }
  rebind_called_ = true;
  return ZX_OK;
}

const char* Bind::DeviceGetName(zx_device_t* device) {
  if (device != kFakeParent) {
    bad_device_ = true;
  }
  return "";
}

zx_off_t Bind::DeviceGetSize(zx_device_t* device) {
  if (device != kFakeParent) {
    bad_device_ = true;
  }
  return size_;
}

}  // namespace fake_ddk

__EXPORT
zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                   zx_device_t** out) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_OK;
  }
  return fake_ddk::Bind::Instance()->DeviceAdd(drv, parent, args, out);
}

__EXPORT
void device_async_remove(zx_device_t* device) {
  if (!fake_ddk::Bind::Instance()) {
    return;
  }
  return fake_ddk::Bind::Instance()->DeviceAsyncRemove(device);
}

__EXPORT
void device_init_reply(zx_device_t* device, zx_status_t status,
                       const device_init_reply_args_t* args) {
  if (!fake_ddk::Bind::Instance()) {
    return;
  }
  return fake_ddk::Bind::Instance()->DeviceInitReply(device, status, args);
}

__EXPORT
void device_unbind_reply(zx_device_t* device) {
  if (!fake_ddk::Bind::Instance()) {
    return;
  }
  fake_ddk::Bind::Instance()->DeviceUnbindReply(device);
}

__EXPORT void device_suspend_reply(zx_device_t* dev, zx_status_t status, uint8_t out_state) {
  if (!fake_ddk::Bind::Instance()) {
    return;
  }
  return fake_ddk::Bind::Instance()->DeviceSuspendComplete(dev, status, out_state);
}

__EXPORT void device_resume_reply(zx_device_t* dev, zx_status_t status, uint8_t out_power_state,
                                  uint32_t out_perf_state) {
  if (!fake_ddk::Bind::Instance()) {
    return;
  }
  return fake_ddk::Bind::Instance()->DeviceResumeComplete(dev, status, out_power_state,
                                                          out_perf_state);
}

__EXPORT
zx_status_t device_add_metadata(zx_device_t* device, uint32_t type, const void* data,
                                size_t length) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_OK;
  }
  return fake_ddk::Bind::Instance()->DeviceAddMetadata(device, type, data, length);
}

__EXPORT
void device_make_visible(zx_device_t* device, const device_make_visible_args_t* args) {
  if (fake_ddk::Bind::Instance()) {
    fake_ddk::Bind::Instance()->DeviceMakeVisible(device);
  }
  return;
}

__EXPORT
zx_status_t device_get_protocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return fake_ddk::Bind::Instance()->DeviceGetProtocol(device, proto_id, protocol);
}
__EXPORT
zx_status_t device_open_protocol_session_multibindable(const zx_device_t* dev, uint32_t proto_id,
                                                       void* protocol) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return fake_ddk::Bind::Instance()->DeviceOpenProtocolSessionMultibindable(dev, proto_id,
                                                                            protocol);
}

__EXPORT
const char* device_get_name(zx_device_t* device) {
  if (!fake_ddk::Bind::Instance()) {
    return nullptr;
  }
  return fake_ddk::Bind::Instance()->DeviceGetName(device);
}

__EXPORT
zx_off_t device_get_size(zx_device_t* device) {
  if (!fake_ddk::Bind::Instance()) {
    return 0;
  }
  return fake_ddk::Bind::Instance()->DeviceGetSize(device);
}

__EXPORT
zx_status_t device_get_metadata(zx_device_t* device, uint32_t type, void* buf, size_t buflen,
                                size_t* actual) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return fake_ddk::Bind::Instance()->DeviceGetMetadata(device, type, buf, buflen, actual);
}

__EXPORT
zx_status_t device_get_metadata_size(zx_device_t* device, uint32_t type, size_t* out_size) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return fake_ddk::Bind::Instance()->DeviceGetMetadataSize(device, type, out_size);
}

__EXPORT
void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
  // This is currently a no-op.
}

__EXPORT
zx_status_t device_get_profile(zx_device_t* device, uint32_t priority, const char* name,
                               zx_handle_t* out_profile) {
  // This is currently a no-op.
  *out_profile = ZX_HANDLE_INVALID;
  return ZX_OK;
}

__EXPORT
zx_status_t device_get_deadline_profile(zx_device_t* device, uint64_t capacity, uint64_t deadline,
                                        uint64_t period, const char* name,
                                        zx_handle_t* out_profile) {
  // This is currently a no-op.
  *out_profile = ZX_HANDLE_INVALID;
  return ZX_OK;
}

__EXPORT
void device_fidl_transaction_take_ownership(fidl_txn_t* txn, device_fidl_txn_t* new_txn) {
  auto fidl_txn = fake_ddk::FromDdkInternalTransaction(ddk::internal::Transaction::FromTxn(txn));

  ZX_ASSERT_MSG(std::holds_alternative<fidl::Transaction*>(fidl_txn),
                "Can only take ownership of transaction once\n");

  auto result = std::get<fidl::Transaction*>(fidl_txn)->TakeOwnership();
  // We call this to mimic what devhost does.
  result->EnableNextDispatch();
  auto new_ddk_txn = fake_ddk::MakeDdkInternalTransaction(std::move(result));
  *new_txn = *new_ddk_txn.DeviceFidlTxn();
}

__EXPORT __WEAK zx_status_t load_firmware(zx_device_t* device, const char* path, zx_handle_t* fw,
                                          size_t* size) {
  // This is currently a no-op.
  *fw = ZX_HANDLE_INVALID;
  *size = fake_ddk::kFakeFWSize;
  return ZX_OK;
}

__EXPORT
zx_status_t device_rebind(zx_device_t* device) {
  if (!fake_ddk::Bind::Instance()) {
    return ZX_OK;
  }
  return fake_ddk::Bind::Instance()->DeviceRebind(device);
}

// Please do not use get_root_resource() in new code. See fxbug.dev/31358.
__EXPORT
zx_handle_t get_root_resource() { return ZX_HANDLE_INVALID; }

extern "C" bool driver_log_severity_enabled_internal(const zx_driver_t* drv,
                                                     fx_log_severity_t flag) {
  return flag >= fake_ddk::kMinLogSeverity;
}

extern "C" void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                      const char* file, int line, const char* msg, va_list args) {
  vfprintf(stdout, msg, args);
  putchar('\n');
  fflush(stdout);
}

extern "C" void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                     const char* file, int line, const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  driver_logvf_internal(drv, flag, file, line, msg, args);
  va_end(args);
}

__EXPORT
__WEAK zx_driver_rec __zircon_driver_rec__ = {
    .ops = {},
    .driver = {},
    .log_flags = 0,
};
