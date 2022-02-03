// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-controller.h"

#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/threads.h>

#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include <fbl/auto_lock.h>
#include <tee-client-api/tee-client-types.h>

#include "ddktl/suspend-txn.h"
#include "optee-client.h"
#include "optee-util.h"
#include "src/devices/tee/drivers/optee/optee-bind.h"
#include "src/devices/tee/drivers/optee/tee-smc.h"

namespace optee {

constexpr int kDefaultNumThreads = 3;
constexpr char kDefaultRoleName[] = "fuchsia.tee.default";

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

void OpteeControllerBase::WaitQueueWait(const uint64_t key) {
  wq_lock_.Acquire();
  if (wait_queue_.find(key) == wait_queue_.end()) {
    wait_queue_.emplace(std::piecewise_construct, std::forward_as_tuple(key),
                        std::forward_as_tuple());
  }
  wq_lock_.Release();

  wait_queue_.at(key).Wait();

  wq_lock_.Acquire();
  wait_queue_.erase(key);
  wq_lock_.Release();
}

void OpteeControllerBase::WaitQueueSignal(const uint64_t key) {
  fbl::AutoLock wq_lock(&wq_lock_);
  if (wait_queue_.find(key) == wait_queue_.end()) {
    wait_queue_.emplace(std::piecewise_construct, std::forward_as_tuple(key),
                        std::forward_as_tuple());
  }
  wait_queue_.at(key).Signal();
}

size_t OpteeControllerBase::WaitQueueSize() const { return wait_queue_.size(); }

std::list<WaitCtx>::iterator OpteeControllerBase::CommandQueueInit() {
  std::list<WaitCtx>::iterator it;
  cq_lock_.Acquire();
  command_queue_.emplace_back();
  // get iterator to inserted element
  it = std::prev(command_queue_.end());
  cq_lock_.Release();

  return it;
}

void OpteeControllerBase::CommandQueueWait(std::list<WaitCtx>::iterator& el) {
  WaitCtx* ctx;
  cq_lock_.Acquire();
  ctx = &*el;
  command_queue_wait_.push_back(ctx);
  cq_lock_.Release();

  ctx->Wait();

  cq_lock_.Acquire();
  // remove our WaitCtx if it is still in wait queue.
  // This may happened if CommandQueueWait was called after the the WaitCtx was actually signaled
  // (race between wait and signaled threads).
  command_queue_wait_.remove(ctx);
  cq_lock_.Release();
}

void OpteeControllerBase::CommandQueueSignal(const std::list<WaitCtx>::iterator& el) {
  fbl::AutoLock cq_lock(&cq_lock_);

  // remove our own context from command queue
  command_queue_.erase(el);

  // signal the first waiting thread if any
  if (!command_queue_wait_.empty()) {
    auto ctx = command_queue_wait_.front();
    // remove WaitCtx from wait queue to void multi signaling the same context.
    command_queue_wait_.pop_front();
    ctx->Signal();
  } else {  // signal context from command queue
    // there is a more probability that last context in queue will get
    // ThreadLimit than the first one.
    // So, let's find the first not signaled WaitCtx from the end.
    for (auto it = command_queue_.rbegin(); it != command_queue_.rend(); ++it) {
      if (!it->Signaled()) {
        it->Signal();
        break;
      }
    }
  }
}

size_t OpteeControllerBase::CommandQueueSize() const { return command_queue_.size(); }
size_t OpteeControllerBase::CommandQueueWaitSize() const { return command_queue_wait_.size(); }

OpteeController::~OpteeController() {
  loop_.Quit();
  loop_.JoinThreads();
  loop_.Shutdown();
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
                    /*offset=*/0, ZX_PAGE_SIZE, &mmio_vmo_paddr, /*addrs_count=*/1, &pmt);
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

zx_status_t OpteeController::SetProfileByRole(thrd_t& thr, const std::string& role) {
  zx_status_t status;

  // TODO(https://fxbug.dev/40858): SetProfileByRole has to be uset do set thread profile by the
  // rule when corresponding API will be ready to use. Use workaround for now: only
  // fuchsia.default and fuchsia.tee.deadline are hardcoded and supported.
  if (role == kDefaultRoleName) {
    // do nothing.
  } else if (role == "fuchsia.tee.media") {
    zx::profile profile;
    status = device_get_deadline_profile(parent(), ZX_USEC(2000), ZX_USEC(2500), ZX_USEC(2500),
                                         "optee", profile.reset_and_get_address());
    if (status != ZX_OK) {
      LOG(WARNING, "could not get deadline profile");
    } else {
      status = zx::unowned_thread(thrd_get_zx_handle(thr))->set_profile(std::move(profile), 0);
      if (status != ZX_OK) {
        LOG(WARNING, "could not set profile %d", status);
      }
    }
  } else {
    LOG(ERROR, "Unsupported thread profile role %s", role.c_str());
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t OpteeController::CreateThreadPool(async::Loop* loop, uint32_t thread_count,
                                              const std::string& role) {
  zx_status_t status;

  for (uint32_t i = 0; i < thread_count; ++i) {
    thrd_t optee_thread;
    char name[192];
    snprintf(name, sizeof(name), "optee-thread-%s-%d", role.c_str(), i);
    LOG(DEBUG, "Starting OPTEE thread %s for role %s...", name, role.c_str());
    status = loop->StartThread(name, &optee_thread);
    if (status != ZX_OK) {
      LOG(ERROR, "could not start optee thread %d of %d", i, thread_count);
      return status;
    }

    status = SetProfileByRole(optee_thread, role);
    if (status != ZX_OK) {
      LOG(ERROR, "could not set role to %s thread: %d", name, status);
      return status;
    }
  }

  return ZX_OK;
}

async_dispatcher_t* OpteeController::GetDispatcherForTa(const Uuid& ta_uuid) {
  auto it = uuid_config_.find(ta_uuid);
  if (it != uuid_config_.end()) {
    LOG(DEBUG, "Assign request to %s to custom pool.", ta_uuid.ToString().c_str());
    return it->second->dispatcher();
  }

  LOG(DEBUG, "Assign request to %s to default pool.", ta_uuid.ToString().c_str());
  return loop_.dispatcher();
}

zx_status_t OpteeController::InitThreadPools() {
  zx_status_t status = ZX_ERR_INTERNAL;
  uint32_t default_pool_size = kDefaultNumThreads;
  size_t metadata_size;
  size_t actual;

  status = device_get_metadata_size(parent(), DEVICE_METADATA_TEE_THREAD_CONFIG, &metadata_size);
  if (status != ZX_OK || metadata_size == 0) {
    LOG(INFO, "No metadata for driver. Use default thread configuration.");
    return CreateThreadPool(&loop_, default_pool_size, kDefaultRoleName);
  }

  auto buffer = std::make_unique<uint8_t[]>(metadata_size);

  status = device_get_metadata(parent(), DEVICE_METADATA_TEE_THREAD_CONFIG, buffer.get(),
                               metadata_size, &actual);
  if (status != ZX_OK && actual != metadata_size) {
    LOG(ERROR, "device_get_metadata failed %d", status);
    return ZX_ERR_INTERNAL;
  }

  // TODO(fxbug.dev/45252): Use FIDL at rest.
  fidl::DecodedMessage<fuchsia_hardware_tee::wire::TeeMetadata> decoded(
      fidl::internal::WireFormatVersion::kV1, buffer.get(), metadata_size);
  if (!decoded.ok()) {
    LOG(ERROR, "Failed to deserialize metadata.");
    return ZX_ERR_INTERNAL;
  }

  fuchsia_hardware_tee::wire::TeeMetadata* metadata = decoded.PrimaryObject();

  LOG(INFO, "Default thread pool size %d, %zu custom thread pools supplied.",
      metadata->default_thread_count(), metadata->custom_threads().count());

  if (metadata->has_default_thread_count() && metadata->default_thread_count() != 0) {
    default_pool_size = metadata->default_thread_count();
  }

  status = CreateThreadPool(&loop_, default_pool_size, kDefaultRoleName);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to create default thread pool: %d", status);
  }

  if (metadata->has_custom_threads()) {
    std::map<std::string, std::list<async::Loop>::iterator> roles;
    for (auto& custom_thread : metadata->custom_threads()) {
      if (!custom_thread.has_count() || custom_thread.count() == 0 || !custom_thread.has_role() ||
          custom_thread.role().empty() || !custom_thread.has_trusted_apps() ||
          custom_thread.trusted_apps().empty()) {
        LOG(WARNING, "Not complete custom thread configuration(some fields are missed).");
        continue;
      }

      std::list<async::Loop>::iterator loop_it;
      std::string role(custom_thread.role().get());
      auto it = roles.find(role);
      if (it != roles.end()) {
        LOG(WARNING, "Multiple declaration of %s thread pool. Appending...", role.c_str());
        loop_it = it->second;
      } else {
        loop_it = custom_loops_.emplace(custom_loops_.end(), &kAsyncLoopConfigNeverAttachToThread);
        roles.emplace(role, loop_it);
      }

      status = CreateThreadPool(&(*loop_it), custom_thread.count(),
                                std::string(custom_thread.role().get()));
      if (status != ZX_OK) {
        LOG(ERROR, "Failed to create thread pool %s: %d", custom_thread.role().get().data(),
            status);
        return status;
      }

      for (auto& app : custom_thread.trusted_apps()) {
        uuid_config_.emplace(app, loop_it);
      }
    }
  }

  return ZX_OK;
}

zx_status_t OpteeController::Bind() {
  zx_status_t status = ZX_ERR_INTERNAL;

  pdev_ = ddk::PDev::FromFragment(parent());
  if (!pdev_.is_valid()) {
    LOG(ERROR, "unable to get pdev protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  sysmem_ = ddk::SysmemProtocolClient(parent(), "sysmem");
  if (!sysmem_.is_valid()) {
    LOG(ERROR, "unable to get sysmem protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  status = InitThreadPools();
  if (status != ZX_OK) {
    return status;
  }

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

zx_status_t OpteeController::DdkOpen(zx_device_t** out_dev, uint32_t flags) {
  // Do not set out_dev because this Controller will handle the FIDL messages
  return ZX_OK;
}

void OpteeController::DdkSuspend(ddk::SuspendTxn txn) {
  loop_.Quit();
  loop_.JoinThreads();
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

zx_status_t OpteeController::TeeConnectToApplication(const uuid_t* application_uuid,
                                                     zx::channel tee_app_request,
                                                     zx::channel service_provider) {
  ZX_DEBUG_ASSERT(application_uuid);
  ZX_DEBUG_ASSERT(tee_app_request.is_valid());
  return ConnectToApplicationInternal(
      Uuid(*application_uuid),
      fidl::ClientEnd<fuchsia_tee_manager::Provider>(std::move(service_provider)),
      fidl::ServerEnd<fuchsia_tee::Application>(std::move(tee_app_request)));
}

void OpteeController::ConnectToDeviceInfo(
    ConnectToDeviceInfoRequestView request,
    [[maybe_unused]] ConnectToDeviceInfoCompleter::Sync& _completer) {
  ZX_DEBUG_ASSERT(request->device_info_request.is_valid());

  fidl::BindServer<fidl::WireServer<fuchsia_tee::DeviceInfo>>(
      loop_.dispatcher(), std::move(request->device_info_request), this);
}

void OpteeController::ConnectToApplication(
    ConnectToApplicationRequestView request,
    [[maybe_unused]] ConnectToApplicationCompleter::Sync& _completer) {
  ConnectToApplicationInternal(Uuid(request->application_uuid),
                               std::move(request->service_provider),
                               std::move(request->application_request));
}

zx_status_t OpteeController::ConnectToApplicationInternal(
    Uuid application_uuid, fidl::ClientEnd<fuchsia_tee_manager::Provider> service_provider,
    fidl::ServerEnd<fuchsia_tee::Application> application_request) {
  ZX_DEBUG_ASSERT(application_request.is_valid());
  LOG(DEBUG, "Request to %s TA", application_uuid.ToString().c_str());

  async_dispatcher_t* dispatcher = GetDispatcherForTa(application_uuid);
  if (dispatcher == nullptr) {
    LOG(ERROR, "Failed to get dispatcher for %s TA.", application_uuid.ToString().c_str());
    return ZX_ERR_INTERNAL;
  }

  fidl::BindServer(
      dispatcher, std::move(application_request),
      std::make_unique<OpteeClient>(this, std::move(service_provider), application_uuid));

  return ZX_OK;
}

void OpteeController::GetOsInfo(GetOsInfoRequestView request, GetOsInfoCompleter::Sync& completer) {
  fidl::Arena allocator;
  fuchsia_tee::wire::OsRevision os_rev(allocator);
  os_rev.set_major(os_revision().major);
  os_rev.set_minor(os_revision().minor);

  fuchsia_tee::wire::OsInfo os_info(allocator);
  os_info.set_uuid(allocator, kOpteeOsUuid);
  os_info.set_revision(allocator, std::move(os_rev));
  os_info.set_is_global_platform_compliant(true);

  completer.Reply(std::move(os_info));
}

OpteeController::CallResult OpteeController::CallWithMessage(const optee::Message& message,
                                                             RpcHandler rpc_handler) {
  CallResult call_result{.return_code = tee_smc::kSmc32ReturnUnknownFunction,
                         .peak_smc_call_duration = zx::duration::infinite_past()};
  union {
    zx_smc_parameters_t params;
    RpcFunctionResult rpc_result;
  } func_call;
  func_call.params = tee_smc::CreateSmcFunctionCall(optee::kCallWithArgFuncId,
                                                    static_cast<uint32_t>(message.paddr() >> 32),
                                                    static_cast<uint32_t>(message.paddr()));

  // create Wait context for thread handling
  auto cur = CommandQueueInit();
  // remove our Wait context from list and signal waiting context.
  auto deferred_action = fit::deferred_action([&]() { this->CommandQueueSignal(cur); });

  while (true) {
    union {
      zx_smc_result_t raw;
      CallWithArgResult response;
      RpcFunctionArgs rpc_args;
    } result;

    const auto start = zx::clock::get_monotonic();
    zx_status_t status = zx_smc_call(secure_monitor_.get(), &func_call.params, &result.raw);
    const auto duration = zx::clock::get_monotonic() - start;

    if (duration > call_result.peak_smc_call_duration) {
      call_result.peak_smc_call_duration = duration;
    }

    if (status != ZX_OK) {
      LOG(ERROR, "unable to invoke SMC");
      return call_result;
    }

    if (result.response.status == kReturnEThreadLimit) {
      CommandQueueWait(cur);
    } else if (optee::IsReturnRpc(result.response.status)) {
      rpc_handler(result.rpc_args, &func_call.rpc_result);
    } else {
      call_result.return_code = result.response.status;
      break;
    }
  }

  return call_result;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = OpteeController::Create;
  return ops;
}();

}  // namespace optee

ZIRCON_DRIVER(optee, optee::driver_ops, "zircon", "0.1");
