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

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_lock.h>
#include <tee-client-api/tee-client-types.h>

#include "ddktl/suspend-txn.h"
#include "optee-client.h"
#include "optee-device-info.h"
#include "optee-util.h"
#include "src/devices/tee/drivers/optee/optee-bind.h"

namespace optee {

namespace fuchsia_tee = ::llcpp::fuchsia::tee;

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
  // The Trusted OS and Rich OS share a dedicated portion of RAM to send messages back and forth. To
  // discover the memory region to use, we ask the platform device for a MMIO representing the TEE's
  // entire dedicated memory region and query the TEE to discover which section of that should be
  // used as the shared memory. The rest of the TEE's memory region is secure.

  static constexpr uint32_t kTeeBtiIndex = 0;
  zx_status_t status = pdev_.GetBti(kTeeBtiIndex, &bti_);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get bti");
    return status;
  }

  // The TEE BTI will be pinned to get the physical address of the shared memory region between the
  // Rich OS and the Trusted OS. This memory region is not used for DMA and only used for message
  // exchange between the two "worlds." As the TEE is not distinct hardware, but rather the CPU
  // operating in a different EL, it cannot be accessing the shared memory region at this time. The
  // Trusted OS can never execute any code unless we explicitly call into it via SMC, and it can
  // only run code during that SMC call. Once the call returns, the Trusted OS is no longer
  // executing any code and will not until the next time we explicitly call into it. The physical
  // addresses acquired from the BTI pinning are only used within the context of the OP-TEE
  // CallWithArgs SMC calls.
  //
  // As the Trusted OS cannot be actively accessing this memory region, it is safe to release from
  // quarantine.
  status = bti_.release_quarantine();
  if (status != ZX_OK) {
    LOG(ERROR, "could not release quarantine bti - %d", status);
    return status;
  }

  // The Secure World memory is located at a fixed physical address in RAM, so we have to request
  // the platform device map the physical vmo for us.
  static constexpr uint32_t kSecureWorldMemoryMmioIndex = 0;
  pdev_mmio_t mmio_dev;
  status = pdev_.GetMmio(kSecureWorldMemoryMmioIndex, &mmio_dev);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get secure world mmio");
    return status;
  }

  // Briefly pin the first page of this VMO to determine the secure world's base physical address.
  zx_paddr_t mmio_vmo_paddr;
  zx::pmt pmt;
  status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, *zx::unowned_vmo(mmio_dev.vmo),
                    /*offset=*/0, ZX_PAGE_SIZE, &mmio_vmo_paddr, /*num_addrs=*/1, &pmt);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to pin secure world memory");
    return status;
  }

  status = pmt.unpin();
  ZX_DEBUG_ASSERT(status == ZX_OK);

  zx_paddr_t secure_world_paddr = mmio_vmo_paddr + mmio_dev.offset;
  size_t secure_world_size = mmio_dev.size;

  // Now that we have the TEE's entire memory range, query the TEE to see which region of it we
  // should use.
  zx_paddr_t shared_mem_paddr;
  size_t shared_mem_size;
  status = DiscoverSharedMemoryConfig(&shared_mem_paddr, &shared_mem_size);

  if (status != ZX_OK) {
    LOG(ERROR, "unable to discover shared memory configuration");
    return status;
  }

  if (shared_mem_paddr < secure_world_paddr ||
      (shared_mem_paddr + shared_mem_size) > (secure_world_paddr + secure_world_size)) {
    LOG(ERROR, "shared memory outside of secure world range");
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Map and pin the just the shared memory region of the secure world memory.
  mmio_buffer_t mmio;
  zx_off_t shared_mem_offset = shared_mem_paddr - mmio_vmo_paddr;
  status = mmio_buffer_init(&mmio, shared_mem_offset, shared_mem_size, mmio_dev.vmo,
                            ZX_CACHE_POLICY_CACHED);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to map secure world memory");
    return status;
  }

  mmio_pinned_buffer_t pinned_mmio;
  status = mmio_buffer_pin(&mmio, bti_.get(), &pinned_mmio);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to pin secure world memory: %d", status);
    return status;
  }

  // Take ownership of the PMT so that we can explicitly unpin.
  pmt_ = zx::pmt(pinned_mmio.pmt);
  status = SharedMemoryManager::Create(ddk::MmioBuffer(mmio), pinned_mmio.paddr,
                                       &shared_memory_manager_);

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

  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    LOG(ERROR, "unable to get composite protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_ = ddk::PDev(composite);
  if (!pdev_.is_valid()) {
    LOG(ERROR, "unable to get pdev protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  sysmem_ = ddk::SysmemProtocolClient(composite, "sysmem");
  if (!sysmem_.is_valid()) {
    LOG(ERROR, "unable to get sysmem protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  // Optional protocol
  rpmb_protocol_client_ = ddk::RpmbProtocolClient(composite, "rpmb");

  static constexpr uint32_t kTrustedOsSmcIndex = 0;
  status = pdev_.GetSmc(kTrustedOsSmcIndex, &secure_monitor_);
  if (status != ZX_OK) {
    LOG(ERROR, "unable to get secure monitor handle");
    return status;
  }

  // TODO(fxbug.dev/13426): Remove this once we have a tee core driver that will discover the TEE OS
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

zx_status_t OpteeController::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_hardware_tee::DeviceConnector::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t OpteeController::DdkOpen(zx_device_t** out_dev, uint32_t flags) {
  // Do not set out_dev because this Controller will handle the FIDL messages
  return ZX_OK;
}

void OpteeController::DdkSuspend(ddk::SuspendTxn txn) {
  // All operations should have been halted when the child devices were suspended.
  shared_memory_manager_ = nullptr;
  zx_status_t status = pmt_.unpin();
  ZX_DEBUG_ASSERT(status == ZX_OK);
  txn.Reply(ZX_OK, txn.requested_state());
}

void OpteeController::DdkUnbind(ddk::UnbindTxn txn) {
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
                                 [[maybe_unused]] ConnectTeeCompleter::Sync& _completer) {
  TeeConnect(std::move(tee_request), std::move(service_provider));
}

void OpteeController::ConnectToDeviceInfo(
    zx::channel device_info_request,
    [[maybe_unused]] ConnectToDeviceInfoCompleter::Sync& _completer) {
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
    [[maybe_unused]] ConnectToApplicationCompleter::Sync& _completer) {
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

ZIRCON_DRIVER(optee, optee::driver_ops, "zircon", "0.1");
