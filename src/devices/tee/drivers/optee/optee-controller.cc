// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-controller.h"

#include <inttypes.h>
#include <lib/fidl-utils/bind.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_lock.h>
#include <tee-client-api/tee-client-types.h>

#include "optee-client.h"
#include "optee-device-info.h"
#include "optee-util.h"

namespace optee {

namespace fuchsia_tee = ::llcpp::fuchsia::tee;

enum {
  kFragmentPdev,
  kFragmentSysmem,
  kFragmentCount,
};

constexpr TEEC_UUID kOpteeOsUuid = {
    0x486178E0, 0xE7F8, 0x11E3, {0xBC, 0x5E, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B}};

static bool IsOpteeApi(const tee_smc::TrustedOsCallUidResult& returned_uid) {
  return returned_uid.uid_0_3 == kOpteeApiUid_0 && returned_uid.uid_4_7 == kOpteeApiUid_1 &&
         returned_uid.uid_8_11 == kOpteeApiUid_2 && returned_uid.uid_12_15 == kOpteeApiUid_3;
}

static bool IsOpteeApiRevisionSupported(const tee_smc::TrustedOsCallRevisionResult& returned_rev) {
  // The cast is unfortunately necessary to mute a compiler warning about an unsigned expression
  // always being greater than 0.
  ZX_DEBUG_ASSERT(returned_rev.minor <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  return returned_rev.major == kOpteeApiRevisionMajor &&
         static_cast<int32_t>(returned_rev.minor) >= static_cast<int32_t>(kOpteeApiRevisionMinor);
}

zx_status_t OpteeController::ValidateApiUid() const {
  static const zx_smc_parameters_t kGetApiFuncCall =
      tee_smc::CreateSmcFunctionCall(tee_smc::kTrustedOsCallUidFuncId);
  union {
    zx_smc_result_t raw;
    tee_smc::TrustedOsCallUidResult uid;
  } result;
  zx_status_t status = zx_smc_call(secure_monitor_.get(), &kGetApiFuncCall, &result.raw);

  return status == ZX_OK ? IsOpteeApi(result.uid) ? ZX_OK : ZX_ERR_NOT_FOUND : status;
}

zx_status_t OpteeController::ValidateApiRevision() const {
  static const zx_smc_parameters_t kGetApiRevisionFuncCall =
      tee_smc::CreateSmcFunctionCall(tee_smc::kTrustedOsCallRevisionFuncId);
  union {
    zx_smc_result_t raw;
    tee_smc::TrustedOsCallRevisionResult revision;
  } result;
  zx_status_t status = zx_smc_call(secure_monitor_.get(), &kGetApiRevisionFuncCall, &result.raw);

  return status == ZX_OK
             ? IsOpteeApiRevisionSupported(result.revision) ? ZX_OK : ZX_ERR_NOT_SUPPORTED
             : status;
}

zx_status_t OpteeController::GetOsRevision() {
  static const zx_smc_parameters_t kGetOsRevisionFuncCall =
      tee_smc::CreateSmcFunctionCall(kGetOsRevisionFuncId);
  union {
    zx_smc_result_t raw;
    GetOsRevisionResult revision;
  } result;
  zx_status_t status = zx_smc_call(secure_monitor_.get(), &kGetOsRevisionFuncCall, &result.raw);

  if (status != ZX_OK) {
    return status;
  }

  os_revision_ = result.revision;

  return ZX_OK;
}

zx_status_t OpteeController::ExchangeCapabilities() {
  uint64_t nonsecure_world_capabilities = 0;
  if (zx_system_get_num_cpus() == 1) {
    nonsecure_world_capabilities |= kNonSecureCapUniprocessor;
  }

  const zx_smc_parameters_t func_call =
      tee_smc::CreateSmcFunctionCall(kExchangeCapabilitiesFuncId, nonsecure_world_capabilities);
  union {
    zx_smc_result_t raw;
    ExchangeCapabilitiesResult response;
  } result;

  zx_status_t status = zx_smc_call(secure_monitor_.get(), &func_call, &result.raw);

  if (status != ZX_OK) {
    return status;
  }

  if (result.response.status != kReturnOk) {
    return ZX_ERR_INTERNAL;
  }

  secure_world_capabilities_ = result.response.secure_world_capabilities;

  return ZX_OK;
}

zx_status_t OpteeController::InitializeSharedMemory() {
  zx_paddr_t shared_mem_start;
  size_t shared_mem_size;
  zx_status_t status = DiscoverSharedMemoryConfig(&shared_mem_start, &shared_mem_size);

  if (status != ZX_OK) {
    LOG(ERROR, "unable to discover shared memory configuration");
    return status;
  }

  static constexpr uint32_t kTeeBtiIndex = 0;
  zx::bti bti;
  status = pdev_get_bti(&pdev_proto_, kTeeBtiIndex, bti.reset_and_get_address());
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get bti");
    return status;
  }

  // The Secure World memory is located at a fixed physical address in RAM, so we have to request
  // the platform device map the physical vmo for us.
  static constexpr uint32_t kSecureWorldMemoryMmioIndex = 0;
  pdev_mmio_t mmio_dev;
  status = pdev_get_mmio(&pdev_proto_, kSecureWorldMemoryMmioIndex, &mmio_dev);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get secure world mmio");
    return status;
  }

  // Briefly pin the first page of this VMO to determine the secure world's base physical address.
  zx_paddr_t mmio_vmo_paddr;
  zx::pmt pmt;
  status = bti.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, *zx::unowned_vmo(mmio_dev.vmo),
                   /*offset=*/0, ZX_PAGE_SIZE, &mmio_vmo_paddr, /*num_addrs=*/1, &pmt);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to pin secure world memory");
    return status;
  }

  status = pmt.unpin();
  ZX_DEBUG_ASSERT(status == ZX_OK);

  zx_paddr_t sw_base_paddr = mmio_vmo_paddr + mmio_dev.offset;
  size_t sw_size = mmio_dev.size;
  if (shared_mem_start < sw_base_paddr ||
      (shared_mem_start + shared_mem_size) > (sw_base_paddr + sw_size)) {
    LOG(ERROR, "shared memory outside of secure world range");
    return ZX_ERR_OUT_OF_RANGE;
  }

  mmio_buffer_t mmio;
  size_t vmo_relative_offset = shared_mem_start - mmio_vmo_paddr;
  status = mmio_buffer_init(&mmio, vmo_relative_offset, shared_mem_size, mmio_dev.vmo,
                            ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to map secure world memory");
    return status;
  }

  status = SharedMemoryManager::Create(shared_mem_start, shared_mem_size, ddk::MmioBuffer(mmio),
                                       std::move(bti), &shared_memory_manager_);

  if (status != ZX_OK) {
    LOG(ERROR, "unable to initialize SharedMemoryManager");
    return status;
  }

  return status;
}

zx_status_t OpteeController::DiscoverSharedMemoryConfig(zx_paddr_t* out_start_addr,
                                                        size_t* out_size) {
  static const zx_smc_parameters_t func_call =
      tee_smc::CreateSmcFunctionCall(kGetSharedMemConfigFuncId);

  union {
    zx_smc_result_t raw;
    GetSharedMemConfigResult response;
  } result;

  zx_status_t status = zx_smc_call(secure_monitor_.get(), &func_call, &result.raw);

  if (status != ZX_OK) {
    return status;
  }

  if (result.response.status != kReturnOk) {
    return ZX_ERR_INTERNAL;
  }

  *out_start_addr = result.response.start;
  *out_size = result.response.size;

  return status;
}

zx_status_t OpteeController::Create(void* ctx, zx_device_t* parent) {
  auto tee = std::make_unique<OpteeController>(parent);

  auto status = tee->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for tee
    __UNUSED auto ptr = tee.release();
  }

  return status;
}

zx_status_t OpteeController::Bind() {
  zx_status_t status = ZX_ERR_INTERNAL;

  composite_protocol_t composite;
  status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get composite protocol");
    return status;
  }

  zx_device_t* fragments[kFragmentCount];
  size_t actual;
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  if (actual != countof(fragments)) {
    LOG(ERROR, "unable to composite_get_fragments()");
    return ZX_ERR_INTERNAL;
  }

  status = device_get_protocol(fragments[kFragmentPdev], ZX_PROTOCOL_PDEV, &pdev_proto_);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get pdev protocol");
    return status;
  }

  status = device_get_protocol(fragments[kFragmentSysmem], ZX_PROTOCOL_SYSMEM, &sysmem_proto_);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get sysmem protocol");
    return status;
  }

  static constexpr uint32_t kTrustedOsSmcIndex = 0;
  status = pdev_get_smc(&pdev_proto_, kTrustedOsSmcIndex, secure_monitor_.reset_and_get_address());
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get secure monitor handle");
    return status;
  }

  // TODO(MTWN-140): Remove this once we have a tee core driver that will discover the TEE OS
  status = ValidateApiUid();
  if (status != ZX_OK) {
    LOG(ERROR, "API UID does not match");
    return status;
  }

  status = ValidateApiRevision();
  if (status != ZX_OK) {
    LOG(ERROR, "API revision not supported");
    return status;
  }

  status = GetOsRevision();
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get Trusted OS revision");
    return status;
  }

  status = ExchangeCapabilities();
  if (status != ZX_OK) {
    LOG(ERROR, "could not exchange capabilities");
    return status;
  }

  status = InitializeSharedMemory();
  if (status != ZX_OK) {
    LOG(ERROR, "could not initialize shared memory");
    return status;
  }

  status = DdkAdd(kDeviceName.data(), DEVICE_ADD_ALLOW_MULTI_COMPOSITE);
  if (status != ZX_OK) {
    LOG(ERROR, "failed to add device");
    return status;
  }

  return ZX_OK;
}

zx_status_t OpteeController::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_hardware_tee::DeviceConnector::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t OpteeController::DdkOpen(zx_device_t** out_dev, uint32_t flags) {
  // Do not set out_dev because this Controller will handle the FIDL messages
  return ZX_OK;
}

void OpteeController::DdkUnbindNew(ddk::UnbindTxn txn) {
  // Initiate the removal of this device and all of its children.
  txn.Reply();
}

void OpteeController::DdkRelease() {
  // devmgr has given up ownership, so we must clean ourself up.
  delete this;
}

zx_status_t OpteeController::TeeConnect(zx::channel tee_device_request,
                                        zx::channel service_provider) {
  ZX_DEBUG_ASSERT(tee_device_request.is_valid());

  // Create a new `OpteeClient` device and hand off client communication to it.
  auto client =
      std::make_unique<OpteeClient>(this, std::move(service_provider),
                                    std::nullopt /* application_uuid */, true /* use_old_api */);

  // Add a child `OpteeClient` device instance and have it immediately start serving
  // `tee_device_request`
  zx_status_t status = client->DdkAdd(ddk::DeviceAddArgs("optee-client")
                                          .set_flags(DEVICE_ADD_INSTANCE)
                                          .set_client_remote(std::move(tee_device_request)));
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for `client`
  [[maybe_unused]] auto client_ptr = client.release();

  return ZX_OK;
}

void OpteeController::ConnectTee(zx::channel service_provider, zx::channel tee_request,
                                 [[maybe_unused]] ConnectTeeCompleter::Sync _completer) {
  TeeConnect(std::move(tee_request), std::move(service_provider));
}

void OpteeController::ConnectToDeviceInfo(
    zx::channel device_info_request,
    [[maybe_unused]] ConnectToDeviceInfoCompleter::Sync _completer) {
  ZX_DEBUG_ASSERT(device_info_request.is_valid());

  // Create a new `OpteeDeviceInfo` device and hand off client communication to it.
  auto device_info = std::make_unique<OpteeDeviceInfo>(this);

  // Add a child `OpteeDeviceInfo` instance device and have it immediately start serving
  // `device_info_request`.
  zx_status_t status = device_info->DdkAdd(ddk::DeviceAddArgs("optee-client")
                                               .set_flags(DEVICE_ADD_INSTANCE)
                                               .set_client_remote(std::move(device_info_request)));
  if (status != ZX_OK) {
    LOG(ERROR, "failed to create device info child");
    return;
  }

  // devmgr is now in charge of the memory for `device_info`
  [[maybe_unused]] auto device_info_ptr = device_info.release();
}

void OpteeController::ConnectToApplication(
    llcpp::fuchsia::tee::Uuid application_uuid, zx::channel service_provider,
    zx::channel application_request,
    [[maybe_unused]] ConnectToApplicationCompleter::Sync _completer) {
  ZX_DEBUG_ASSERT(application_request.is_valid());

  // Create a new `OpteeClient` device and hand off client communication to it.
  auto client = std::make_unique<OpteeClient>(this, std::move(service_provider),
                                              Uuid(application_uuid), false /* use_old_api */);

  // Add a child `OpteeClient` device instance and have it immediately start serving
  // `device_request`
  zx_status_t status = client->DdkAdd(ddk::DeviceAddArgs("optee-client")
                                          .set_flags(DEVICE_ADD_INSTANCE)
                                          .set_client_remote(std::move(application_request)));
  if (status != ZX_OK) {
    LOG(ERROR, "failed to create device info child (status: %d)", status);
    return;
  }

  // devmgr is now in charge of the memory for `client`
  [[maybe_unused]] auto client_ptr = client.release();
}

OsInfo OpteeController::GetOsInfo() const {
  fuchsia_tee::Uuid uuid;
  uuid.time_low = kOpteeOsUuid.timeLow;
  uuid.time_mid = kOpteeOsUuid.timeMid;
  uuid.time_hi_and_version = kOpteeOsUuid.timeHiAndVersion;
  std::memcpy(uuid.clock_seq_and_node.data(), kOpteeOsUuid.clockSeqAndNode,
              sizeof(uuid.clock_seq_and_node));

  OsRevision os_revision;
  os_revision.set_major(os_revision_.major);
  os_revision.set_minor(os_revision_.minor);

  OsInfo os_info;
  os_info.set_uuid(uuid);
  os_info.set_revision(std::move(os_revision));
  os_info.set_is_global_platform_compliant(true);
  return os_info;
}

uint32_t OpteeController::CallWithMessage(const optee::Message& message, RpcHandler rpc_handler) {
  uint32_t return_value = tee_smc::kSmc32ReturnUnknownFunction;
  union {
    zx_smc_parameters_t params;
    RpcFunctionResult rpc_result;
  } func_call;
  func_call.params = tee_smc::CreateSmcFunctionCall(optee::kCallWithArgFuncId,
                                                    static_cast<uint32_t>(message.paddr() >> 32),
                                                    static_cast<uint32_t>(message.paddr()));

  while (true) {
    union {
      zx_smc_result_t raw;
      CallWithArgResult response;
      RpcFunctionArgs rpc_args;
    } result;

    zx_status_t status = zx_smc_call(secure_monitor_.get(), &func_call.params, &result.raw);
    if (status != ZX_OK) {
      LOG(ERROR, "unable to invoke SMC");
      return return_value;
    }

    if (result.response.status == kReturnEThreadLimit) {
      // TODO(rjascani): This should actually block until a thread is available. For now,
      // just quit.
      LOG(ERROR, "hit thread limit, need to fix this");
      break;
    } else if (optee::IsReturnRpc(result.response.status)) {
      rpc_handler(result.rpc_args, &func_call.rpc_result);
    } else {
      return_value = result.response.status;
      break;
    }
  }

  return return_value;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = OpteeController::Create;
  return ops;
}();

}  // namespace optee

// clang-format off
ZIRCON_DRIVER_BEGIN(optee, optee::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_OPTEE),
ZIRCON_DRIVER_END(optee)
    // clang-format on
