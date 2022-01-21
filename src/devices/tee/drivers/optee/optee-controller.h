// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_CONTROLLER_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_CONTROLLER_H_

#include <fidl/fuchsia.hardware.rpmb/cpp/wire.h>
#include <fidl/fuchsia.hardware.tee/cpp/wire.h>
#include <fidl/fuchsia.tee.manager/cpp/wire.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/hardware/tee/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/fit/function.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>

#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <thread>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "optee-message.h"
#include "optee-smc.h"
#include "optee-util.h"
#include "shared-memory.h"

namespace optee {

class OpteeClient;
class OpteeDeviceInfo;

class WaitCtx {
 public:
  WaitCtx() = default;
  WaitCtx(const WaitCtx&) = delete;
  WaitCtx& operator=(const WaitCtx*) = delete;

  void Wait() {
    std::unique_lock<std::mutex> lck(lock_);
    cv_.wait(lck, [this] { return done_; });
  }
  void Signal() {
    {
      std::unique_lock<std::mutex> lck(lock_);
      done_ = true;
    }
    cv_.notify_one();
  }

  bool Signaled() {
    std::unique_lock<std::mutex> lck(lock_);
    return done_;
  }

 private:
  bool done_{false};
  std::mutex lock_;
  std::condition_variable cv_;
};

class OpteeControllerBase {
 public:
  using RpcHandler = fit::function<zx_status_t(const RpcFunctionArgs&, RpcFunctionResult*)>;

  // Helper struct for the return value of CallWithMessage.
  struct CallResult {
    uint32_t return_code;
    // For each CallWithMessage, the OpteeController will likely make several invocations of
    // zx_smc_call. This is usually due to interrupts or RPC calls and we'll re-enter the secure
    // world with a subsequent zx_smc_call. The peak_smc_call_duration will capture the duration of
    // the longest zx_smc_call invocation for debugging purposes.
    zx::duration peak_smc_call_duration;
  };

  virtual CallResult CallWithMessage(const optee::Message& message, RpcHandler rpc_handler) = 0;
  virtual SharedMemoryManager::DriverMemoryPool* driver_pool() const = 0;
  virtual SharedMemoryManager::ClientMemoryPool* client_pool() const = 0;

  void WaitQueueWait(uint64_t key);
  void WaitQueueSignal(uint64_t key);
  size_t WaitQueueSize() const;
  std::list<WaitCtx>::iterator CommandQueueInit();
  void CommandQueueWait(std::list<WaitCtx>::iterator& el);
  void CommandQueueSignal(const std::list<WaitCtx>::iterator& el);
  size_t CommandQueueSize() const;
  size_t CommandQueueWaitSize() const;

  virtual zx_status_t RpmbConnectServer(
      fidl::ServerEnd<fuchsia_hardware_rpmb::Rpmb> server) const = 0;
  virtual zx_device_t* GetDevice() const = 0;

 private:
  fbl::Mutex wq_lock_;
  std::map<uint64_t, WaitCtx> wait_queue_;

  fbl::Mutex cq_lock_;
  std::list<WaitCtx> command_queue_;
  std::list<WaitCtx*> command_queue_wait_;
};

class OpteeController;
using DeviceType =
    ddk::Device<OpteeController, ddk::Messageable<fuchsia_hardware_tee::DeviceConnector>::Mixin,
                ddk::Openable, ddk::Suspendable, ddk::Unbindable>;
class OpteeController : public OpteeControllerBase,
                        public DeviceType,
                        public ddk::TeeProtocol<OpteeController, ddk::base_protocol>,
                        public fidl::WireServer<fuchsia_tee::DeviceInfo> {
 public:
  explicit OpteeController(zx_device_t* parent)
      : DeviceType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  ~OpteeController() override;
  OpteeController(const OpteeController&) = delete;
  OpteeController& operator=(const OpteeController&) = delete;

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Bind();

  zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // fuchsia.hardware.Tee
  zx_status_t TeeConnectToApplication(const uuid_t* application_uuid, zx::channel tee_app_request,
                                      zx::channel service_provider);

  // `DeviceConnector` FIDL protocol
  void ConnectToDeviceInfo(ConnectToDeviceInfoRequestView request,
                           ConnectToDeviceInfoCompleter::Sync& _completer) override;
  void ConnectToApplication(ConnectToApplicationRequestView request,
                            ConnectToApplicationCompleter::Sync& _completer) override;

  // `DeviceInfo` FIDL protocol
  void GetOsInfo(GetOsInfoRequestView request, GetOsInfoCompleter::Sync& completer) override;

  CallResult CallWithMessage(const optee::Message& message, RpcHandler rpc_handler) override;

  SharedMemoryManager::DriverMemoryPool* driver_pool() const override {
    return shared_memory_manager_->driver_pool();
  }

  SharedMemoryManager::ClientMemoryPool* client_pool() const override {
    return shared_memory_manager_->client_pool();
  }

  zx_device_t* GetDevice() const override { return zxdev(); }

  zx_status_t RpmbConnectServer(
      fidl::ServerEnd<fuchsia_hardware_rpmb::Rpmb> server) const override {
    if (!server.is_valid()) {
      return ZX_ERR_INVALID_ARGS;
    }

    return DdkConnectFragmentFidlProtocol("rpmb", std::move(server));
  }

  const GetOsRevisionResult& os_revision() const { return os_revision_; }

  // Should only be used for testing.
  const zx::pmt& pmt() const { return pmt_; }

 private:
  static constexpr fuchsia_tee::wire::Uuid kOpteeOsUuid = {
      0x486178E0, 0xE7F8, 0x11E3, {0xBC, 0x5E, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B}};

  zx_status_t InitThreadPools();
  zx_status_t SetProfileByRole(thrd_t& thr, const std::string& role);
  zx_status_t CreateThreadPool(async::Loop* loop, uint32_t thread_count, const std::string& role);
  async_dispatcher_t* GetDispatcherForTa(const Uuid& ta_uuid);

  zx_status_t ValidateApiUid() const;
  zx_status_t ValidateApiRevision() const;
  zx_status_t GetOsRevision();
  zx_status_t ExchangeCapabilities();
  zx_status_t InitializeSharedMemory();
  zx_status_t DiscoverSharedMemoryConfig(zx_paddr_t* out_start_addr, size_t* out_size);

  zx_status_t ConnectToApplicationInternal(
      Uuid application_uuid, fidl::ClientEnd<fuchsia_tee_manager::Provider> service_provider,
      fidl::ServerEnd<fuchsia_tee::Application> application_request);

  ddk::PDev pdev_;
  async::Loop loop_;
  std::list<async::Loop> custom_loops_;
  std::map<Uuid, std::list<async::Loop>::iterator> uuid_config_;

  ddk::SysmemProtocolClient sysmem_;
  zx::resource secure_monitor_;
  uint32_t secure_world_capabilities_ = 0;
  GetOsRevisionResult os_revision_;
  zx::bti bti_;
  zx::pmt pmt_;
  std::unique_ptr<SharedMemoryManager> shared_memory_manager_;
};

}  // namespace optee

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_CONTROLLER_H_
