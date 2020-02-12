// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_CONTROLLER_H_
#define SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_CONTROLLER_H_

#include <fuchsia/hardware/tee/llcpp/fidl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>

#include <memory>

#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/tee.h>
#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "optee-message.h"
#include "optee-smc.h"
#include "shared-memory.h"

namespace optee {

namespace fuchsia_hardware_tee = ::llcpp::fuchsia::hardware::tee;

class OpteeClient;
class OpteeDeviceInfo;

class OpteeController;
using OpteeControllerBase =
    ddk::Device<OpteeController, ddk::Messageable, ddk::Openable, ddk::UnbindableNew>;
class OpteeController : public OpteeControllerBase,
                        public ddk::TeeProtocol<OpteeController, ddk::base_protocol>,
                        public fuchsia_hardware_tee::DeviceConnector::Interface {
 public:
  using RpcHandler = fbl::Function<zx_status_t(const RpcFunctionArgs&, RpcFunctionResult*)>;

  explicit OpteeController(zx_device_t* parent) : OpteeControllerBase(parent) {}

  OpteeController(const OpteeController&) = delete;
  OpteeController& operator=(const OpteeController&) = delete;

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Bind();

  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  // ddk.protocol.Tee
  zx_status_t TeeConnect(zx::channel tee_device_request, zx::channel service_provider);

  // `DeviceConnector` FIDL protocol
  void ConnectTee(zx::channel service_provider, zx::channel tee_request,
                  ConnectTeeCompleter::Sync _completer) override;
  void ConnectToDeviceInfo(::zx::channel device_info_request,
                           ConnectToDeviceInfoCompleter::Sync _completer) override;
  void ConnectToApplication(llcpp::fuchsia::tee::Uuid application_uuid,
                            zx::channel service_provider, zx::channel application_request,
                            ConnectToApplicationCompleter::Sync _completer) override;

  // TODO(44664): Once all clients are transitioned off of the old TEE connection model, remove this
  // function.
  OsInfo GetOsInfo() const;

  uint32_t CallWithMessage(const optee::Message& message, RpcHandler rpc_handler);

  SharedMemoryManager::DriverMemoryPool* driver_pool() const {
    return shared_memory_manager_->driver_pool();
  }

  SharedMemoryManager::ClientMemoryPool* client_pool() const {
    return shared_memory_manager_->client_pool();
  }

  const GetOsRevisionResult& os_revision() const { return os_revision_; }

 private:
  zx_status_t ValidateApiUid() const;
  zx_status_t ValidateApiRevision() const;
  zx_status_t GetOsRevision();
  zx_status_t ExchangeCapabilities();
  zx_status_t InitializeSharedMemory();
  zx_status_t DiscoverSharedMemoryConfig(zx_paddr_t* out_start_addr, size_t* out_size);

  pdev_protocol_t pdev_proto_ = {};
  sysmem_protocol_t sysmem_proto_ = {};
  zx::resource secure_monitor_;
  uint32_t secure_world_capabilities_ = 0;
  GetOsRevisionResult os_revision_;
  std::unique_ptr<SharedMemoryManager> shared_memory_manager_;
};

}  // namespace optee

#endif  // SRC_DEVICES_TEE_DRIVERS_OPTEE_OPTEE_CONTROLLER_H_
