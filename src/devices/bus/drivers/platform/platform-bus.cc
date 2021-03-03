// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/platform/platform-bus.h"

#include <assert.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/driver-config.h>
#include <zircon/process.h>
#include <zircon/syscalls/iommu.h>

#include <algorithm>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

#include "src/devices/bus/drivers/platform/cpu-trace.h"
#include "src/devices/bus/drivers/platform/platform-bus-bind.h"

namespace platform_bus {

zx_status_t PlatformBus::IommuGetBti(uint32_t iommu_index, uint32_t bti_id, zx::bti* out_bti) {
  if (iommu_index != 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  std::pair key(iommu_index, bti_id);
  auto bti = cached_btis_.find(key);
  if (bti == cached_btis_.end()) {
    zx::bti new_bti;
    zx_status_t status = zx::bti::create(iommu_handle_, 0, bti_id, &new_bti);
    if (status != ZX_OK) {
      return status;
    }
    auto [iter, _] = cached_btis_.emplace(key, std::move(new_bti));
    bti = iter;
  }

  return bti->second.duplicate(ZX_RIGHT_SAME_RIGHTS, out_bti);
}

zx_status_t PlatformBus::PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol,
                                              size_t protocol_size) {
  if (!protocol || protocol_size < sizeof(ddk::AnyProtocol)) {
    return ZX_ERR_INVALID_ARGS;
  }

  switch (proto_id) {
      // DO NOT ADD ANY MORE PROTOCOLS HERE.
      // SYSMEM is needed for the x86 board driver and GPIO_IMPL is needed for board driver
      // pinmuxing. IOMMU is for potential future use. CLOCK_IMPL and POWER_IMPL are needed by the
      // mt8167s board driver. Use of this mechanism for all other protocols has been deprecated.
    case ZX_PROTOCOL_CLOCK_IMPL: {
      clock_ =
          ddk::ClockImplProtocolClient(reinterpret_cast<const clock_impl_protocol_t*>(protocol));
      break;
    }
    case ZX_PROTOCOL_GPIO_IMPL: {
      gpio_ = ddk::GpioImplProtocolClient(reinterpret_cast<const gpio_impl_protocol_t*>(protocol));
      break;
    }
    case ZX_PROTOCOL_IOMMU: {
      iommu_ = ddk::IommuProtocolClient(reinterpret_cast<const iommu_protocol_t*>(protocol));
      break;
    }
    case ZX_PROTOCOL_POWER_IMPL: {
      power_ =
          ddk::PowerImplProtocolClient(reinterpret_cast<const power_impl_protocol_t*>(protocol));
      break;
    }
    case ZX_PROTOCOL_SYSMEM: {
      sysmem_ = ddk::SysmemProtocolClient(reinterpret_cast<const sysmem_protocol_t*>(protocol));
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&proto_completion_mutex_);
  sync_completion_signal(&proto_completion_);
  return ZX_OK;
}

zx_status_t PlatformBus::PBusDeviceAdd(const pbus_dev_t* pdev) {
  if (!pdev->name) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_device_t* parent_dev;
  if (pdev->vid == PDEV_VID_GENERIC && pdev->pid == PDEV_PID_GENERIC &&
      pdev->did == PDEV_DID_KPCI) {
    // Add PCI root at top level.
    parent_dev = parent();
  } else {
    parent_dev = zxdev();
  }

  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto status = PlatformDevice::Create(pdev, parent_dev, this, PlatformDevice::Isolated, &dev);
  if (status != ZX_OK) {
    return status;
  }

  status = dev->Start();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

zx_status_t PlatformBus::PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* pdev) {
  if (!pdev->name) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto status = PlatformDevice::Create(pdev, zxdev(), this, PlatformDevice::Protocol, &dev);
  if (status != ZX_OK) {
    return status;
  }

  status = dev->Start();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();

  // Wait for protocol implementation driver to register its protocol.
  ddk::AnyProtocol dummy_proto;

  proto_completion_mutex_.Acquire();
  while (DdkGetProtocol(proto_id, &dummy_proto) == ZX_ERR_NOT_SUPPORTED) {
    sync_completion_reset(&proto_completion_);
    proto_completion_mutex_.Release();
    zx_status_t status = sync_completion_wait(&proto_completion_, ZX_SEC(10));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s sync_completion_wait(protocol %08x) failed: %d", __FUNCTION__, proto_id,
             status);
      return status;
    }
    proto_completion_mutex_.Acquire();
  }
  proto_completion_mutex_.Release();
  return ZX_OK;
}

zx_status_t PlatformBus::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_sysinfo::SysInfo::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void PlatformBus::GetBoardName(GetBoardNameCompleter::Sync& completer) {
  fbl::AutoLock lock(&board_info_lock_);
  // Reply immediately if board_name is valid.
  if (board_info_.board_name[0]) {
    completer.Reply(ZX_OK,
                    fidl::StringView(board_info_.board_name, strlen(board_info_.board_name)));
    return;
  }
  // Cache the requests until board_name becomes valid.
  board_name_completer_.push_back(completer.ToAsync());
}

void PlatformBus::GetBoardRevision(GetBoardRevisionCompleter::Sync& completer) {
  fbl::AutoLock lock(&board_info_lock_);
  completer.Reply(ZX_OK, board_info_.board_revision);
}

void PlatformBus::GetBootloaderVendor(GetBootloaderVendorCompleter::Sync& completer) {
  fbl::AutoLock lock(&bootloader_info_lock_);
  // Reply immediately if vendor is valid.
  if (bootloader_info_.vendor[0]) {
    completer.Reply(ZX_OK,
                    fidl::StringView(bootloader_info_.vendor, strlen(bootloader_info_.vendor)));
    return;
  }
  // Cache the requests until vendor becomes valid.
  bootloader_vendor_completer_.push_back(completer.ToAsync());
}

void PlatformBus::GetInterruptControllerInfo(GetInterruptControllerInfoCompleter::Sync& completer) {
  ::fuchsia_sysinfo::wire::InterruptControllerInfo info = {
      .type = interrupt_controller_type_,
  };
  completer.Reply(ZX_OK, fidl::unowned_ptr(&info));
}

zx_status_t PlatformBus::PBusGetBoardInfo(pdev_board_info_t* out_info) {
  fbl::AutoLock lock(&board_info_lock_);
  memcpy(out_info, &board_info_, sizeof(board_info_));
  return ZX_OK;
}

zx_status_t PlatformBus::PBusSetBoardInfo(const pbus_board_info_t* info) {
  fbl::AutoLock lock(&board_info_lock_);
  if (info->board_name[0]) {
    strlcpy(board_info_.board_name, info->board_name, sizeof(board_info_.board_name));
    zxlogf(INFO, "PlatformBus: set board name to \"%s\"", board_info_.board_name);

    std::vector<GetBoardNameCompleter::Async> completer_tmp_;
    // Respond to pending boardname requests, if any.
    board_name_completer_.swap(completer_tmp_);
    while (!completer_tmp_.empty()) {
      completer_tmp_.back().Reply(
          ZX_OK, fidl::StringView(board_info_.board_name, strlen(board_info_.board_name)));
      completer_tmp_.pop_back();
    }
  }
  board_info_.board_revision = info->board_revision;
  return ZX_OK;
}

zx_status_t PlatformBus::PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
  fbl::AutoLock lock(&bootloader_info_lock_);
  if (info->vendor[0]) {
    strlcpy(bootloader_info_.vendor, info->vendor, sizeof(bootloader_info_.vendor));
    zxlogf(INFO, "PlatformBus: set bootloader vendor to \"%s\"", bootloader_info_.vendor);

    std::vector<GetBootloaderVendorCompleter::Async> completer_tmp_;
    // Respond to pending boardname requests, if any.
    bootloader_vendor_completer_.swap(completer_tmp_);
    while (!completer_tmp_.empty()) {
      completer_tmp_.back().Reply(
          ZX_OK, fidl::StringView(bootloader_info_.vendor, strlen(bootloader_info_.vendor)));
      completer_tmp_.pop_back();
    }
  }
  return ZX_OK;
}

zx_status_t PlatformBus::PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
  suspend_cb_ = *suspend_cbin;
  return ZX_OK;
}

zx_status_t PlatformBus::PBusCompositeDeviceAdd(
    const pbus_dev_t* pdev,
    /* const device_fragment_t* */ uint64_t raw_fragments_list, size_t fragments_count,
    uint32_t coresident_device_index) {
  if (!pdev || !pdev->name) {
    return ZX_ERR_INVALID_ARGS;
  }

  const device_fragment_t* fragments_list =
      reinterpret_cast<const device_fragment_t*>(raw_fragments_list);

  // Do not allow adding composite devices in our devhost.
  // The index must be greater than zero to specify one of the other fragments, or UINT32_MAX
  // to create a new devhost.
  if (coresident_device_index == 0) {
    zxlogf(ERROR, "%s: coresident_device_index cannot be zero", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<platform_bus::PlatformDevice> dev;
  auto status = PlatformDevice::Create(pdev, zxdev(), this, PlatformDevice::Fragment, &dev);
  if (status != ZX_OK) {
    return status;
  }

  status = dev->Start();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();

  constexpr size_t kMaxFragments = 100;
  if (fragments_count + 1 > kMaxFragments) {
    zxlogf(ERROR, "Too many fragments requested.");
    return ZX_ERR_INVALID_ARGS;
  }
  device_fragment_t fragments[kMaxFragments];
  memcpy(&fragments[1], fragments_list, fragments_count * sizeof(fragments[1]));

  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  const zx_bind_inst_t pdev_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, pdev->vid),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, pdev->pid),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, pdev->did),
      BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_INSTANCE_ID, pdev->instance_id),
  };
  const device_fragment_part_t pdev_fragment[] = {
      {countof(root_match), root_match},
      {countof(pdev_match), pdev_match},
  };

  fragments[0].name = "fuchsia.hardware.platform.device.PDev";
  fragments[0].parts_count = std::size(pdev_fragment);
  fragments[0].parts = pdev_fragment;

  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, pdev->vid},
      {BIND_PLATFORM_DEV_PID, 0, pdev->pid},
      {BIND_PLATFORM_DEV_DID, 0, pdev->did},
      {BIND_PLATFORM_DEV_INSTANCE_ID, 0, pdev->instance_id},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = fragments,
      .fragments_count = fragments_count + 1,
      .coresident_device_index = coresident_device_index,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  return DdkAddComposite(pdev->name, &comp_desc);
}

zx_status_t PlatformBus::DdkGetProtocol(uint32_t proto_id, void* out) {
  switch (proto_id) {
      // DO NOT ADD ANY MORE PROTOCOLS HERE.
      // SYSMEM is needed for the x86 board driver and GPIO_IMPL is needed for board driver
      // pinmuxing. IOMMU is for potential future use. CLOCK_IMPL and POWER_IMPL are needed by the
      // mt8167s board driver. Use of this mechanism for all other protocols has been deprecated.
    case ZX_PROTOCOL_PBUS: {
      auto proto = static_cast<pbus_protocol_t*>(out);
      proto->ctx = this;
      proto->ops = &pbus_protocol_ops_;
      return ZX_OK;
    }
    case ZX_PROTOCOL_CLOCK_IMPL:
      if (clock_) {
        clock_->GetProto(static_cast<clock_impl_protocol_t*>(out));
        return ZX_OK;
      }
      break;
    case ZX_PROTOCOL_GPIO_IMPL:
      if (gpio_) {
        gpio_->GetProto(static_cast<gpio_impl_protocol_t*>(out));
        return ZX_OK;
      }
      break;
    case ZX_PROTOCOL_SYSMEM:
      if (sysmem_) {
        sysmem_->GetProto(static_cast<sysmem_protocol_t*>(out));
        return ZX_OK;
      }
      break;
    case ZX_PROTOCOL_POWER_IMPL:
      if (power_) {
        power_->GetProto(static_cast<power_impl_protocol_t*>(out));
        return ZX_OK;
      }
      break;
    case ZX_PROTOCOL_IOMMU:
      if (iommu_) {
        iommu_->GetProto(static_cast<iommu_protocol_t*>(out));
        return ZX_OK;
      } else {
        // return default implementation
        auto proto = static_cast<iommu_protocol_t*>(out);
        proto->ctx = this;
        proto->ops = &iommu_protocol_ops_;
        return ZX_OK;
      }
      break;
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PlatformBus::GetBootItem(uint32_t type, uint32_t extra, zx::vmo* vmo,
                                     uint32_t* length) {
  auto result = fuchsia_boot::Items::Call::Get(zx::unowned(items_svc_), type, extra);
  if (result.ok()) {
    *vmo = std::move(result->payload);
    *length = result->length;
  }
  return result.status();
}

zx_status_t PlatformBus::GetBootItem(uint32_t type, uint32_t extra, fbl::Array<uint8_t>* out) {
  zx::vmo vmo;
  uint32_t length;
  zx_status_t status = GetBootItem(type, extra, &vmo, &length);
  if (status != ZX_OK) {
    return status;
  }
  if (vmo.is_valid()) {
    fbl::Array<uint8_t> data(new uint8_t[length], length);
    status = vmo.read(data.data(), 0, data.size());
    if (status != ZX_OK) {
      return status;
    }
    *out = std::move(data);
  }
  return ZX_OK;
}

void PlatformBus::DdkRelease() { delete this; }

typedef struct {
  void* pbus_instance;
  zx_device_t* sys_root;
} sysdev_suspend_t;

static void sys_device_suspend(void* ctx, uint8_t requested_state, bool enable_wake,
                               uint8_t suspend_reason) {
  auto* p = reinterpret_cast<sysdev_suspend_t*>(ctx);
  auto* pbus = reinterpret_cast<class PlatformBus*>(p->pbus_instance);

  if (pbus != nullptr) {
    pbus_sys_suspend_t suspend_cb = pbus->suspend_cb();
    if (suspend_cb.callback != nullptr) {
      uint8_t out_state = 0;
      zx_status_t status = suspend_cb.callback(suspend_cb.ctx, requested_state, enable_wake,
                                               suspend_reason, &out_state);
      device_suspend_reply(p->sys_root, status, out_state);
      return;
    }
  }
  device_suspend_reply(p->sys_root, ZX_OK, 0);
}

static void sys_device_release(void* ctx) {
  auto* p = reinterpret_cast<sysdev_suspend_t*>(ctx);
  delete p;
}

// cpu-trace provides access to the cpu's tracing and performance counters.
// As such the "device" is the cpu itself.
static void InitCpuTrace(zx_device_t* parent, const zx::iommu& dummy_iommu) {
  zx::bti cpu_trace_bti;
  zx_status_t status = zx::bti::create(dummy_iommu, 0, CPU_TRACE_BTI_ID, &cpu_trace_bti);
  if (status != ZX_OK) {
    // This is not fatal.
    zxlogf(ERROR, "platform-bus: error %d in bti_create(cpu_trace_bti)", status);
    return;
  }

  status = publish_cpu_trace(cpu_trace_bti.release(), parent);
  if (status != ZX_OK) {
    // This is not fatal.
    zxlogf(INFO, "publish_cpu_trace returned %d", status);
  }
}

static zx_protocol_device_t sys_device_proto = []() {
  zx_protocol_device_t result = {};

  result.version = DEVICE_OPS_VERSION;
  result.suspend = sys_device_suspend;
  result.release = sys_device_release;
  return result;
}();

zx_status_t PlatformBus::Create(zx_device_t* parent, const char* name, zx::channel items_svc) {
  // This creates the "sys" device.
  sys_device_proto.version = DEVICE_OPS_VERSION;

  // The suspend op needs to get access to the PBus instance, to be able to
  // callback the ACPI suspend hook. Introducing a level of indirection here
  // to allow us to update the PBus instance in the device context after creating
  // the device.
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> ptr(new (&ac) uint8_t[sizeof(sysdev_suspend_t)]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto* suspend_buf = reinterpret_cast<sysdev_suspend_t*>(ptr.get());
  suspend_buf->pbus_instance = nullptr;

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "sys";
  args.ops = &sys_device_proto;
  args.flags = DEVICE_ADD_NON_BINDABLE;
  args.ctx = suspend_buf;

  // Create /dev/sys.
  auto status = device_add(parent, &args, &suspend_buf->sys_root);
  if (status != ZX_OK) {
    return status;
  } else {
    __UNUSED auto* dummy = ptr.release();
  }

  // Add child of sys for the board driver to bind to.
  std::unique_ptr<platform_bus::PlatformBus> bus(
      new (&ac) platform_bus::PlatformBus(suspend_buf->sys_root, std::move(items_svc)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  suspend_buf->pbus_instance = bus.get();

  status = bus->Init();
  if (status != ZX_OK) {
    return status;
  }

  // Create /dev/sys/cpu-trace.
  // But only do so if we have an iommu handle. Normally we do, but tests
  // may create us without a root resource, and thus without the iommu
  // handle.
  if (bus->iommu_handle_.is_valid()) {
    // Failure is not fatal. Error message already printed.
    InitCpuTrace(suspend_buf->sys_root, bus->iommu_handle_);
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = bus.release();
  return ZX_OK;
}

PlatformBus::PlatformBus(zx_device_t* parent, zx::channel items_svc)
    : PlatformBusType(parent), items_svc_(std::move(items_svc)) {
  sync_completion_reset(&proto_completion_);
}

zx_status_t PlatformBus::GetBoardInfo(zbi_board_info_t* board_info) {
  zx::vmo vmo;
  uint32_t len;
  zx_status_t status = GetBootItem(ZBI_TYPE_DRV_BOARD_INFO, 0, &vmo, &len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Boot Item ZBI_TYPE_DRV_BOARD_INFO not found");
    return status;
  }
  if (!vmo.is_valid()) {
    zxlogf(ERROR, "Invalid zbi_board_info_t VMO");
    return ZX_ERR_UNAVAILABLE;
  }
  status = vmo.read(board_info, 0, std::min<uint64_t>(len, sizeof(*board_info)));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read zbi_board_info_t VMO");
  }
  return status;
}

zx_status_t PlatformBus::Init() {
  zx_status_t status;
  // Set up a dummy IOMMU protocol to use in the case where our board driver
  // does not set a real one.
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  if (root_resource->is_valid()) {
    status =
        zx::iommu::create(*root_resource, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu_handle_);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Read kernel driver.
  zx::vmo vmo;
  uint32_t length;
#if __x86_64__
  interrupt_controller_type_ = ::fuchsia_sysinfo::wire::InterruptControllerType::APIC;
#else
  status = GetBootItem(ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V2, &vmo, &length);
  if (status != ZX_OK) {
    return status;
  }
  if (vmo.is_valid()) {
    interrupt_controller_type_ = ::fuchsia_sysinfo::wire::InterruptControllerType::GIC_V2;
  }
  status = GetBootItem(ZBI_TYPE_KERNEL_DRIVER, KDRV_ARM_GIC_V3, &vmo, &length);
  if (status != ZX_OK) {
    return status;
  }
  if (vmo.is_valid()) {
    interrupt_controller_type_ = ::fuchsia_sysinfo::wire::InterruptControllerType::GIC_V3;
  }
#endif

  // Read platform ID.
  status = GetBootItem(ZBI_TYPE_PLATFORM_ID, 0, &vmo, &length);
  if (status != ZX_OK) {
    return status;
  }

#if __aarch64__
  {
    // For arm64, we do not expect a board to set the bootloader info.
    fbl::AutoLock lock(&bootloader_info_lock_);
    auto vendor = "<unknown>";
    strlcpy(bootloader_info_.vendor, vendor, sizeof(vendor));
  }
#endif

  fbl::AutoLock lock(&board_info_lock_);
  if (vmo.is_valid()) {
    zbi_platform_id_t platform_id;
    status = vmo.read(&platform_id, 0, sizeof(platform_id));
    if (status != ZX_OK) {
      return status;
    }
    zxlogf(INFO, "platform bus: VID: %u PID: %u board: \"%s\"", platform_id.vid, platform_id.pid,
           platform_id.board_name);
    board_info_.vid = platform_id.vid;
    board_info_.pid = platform_id.pid;
    memcpy(board_info_.board_name, platform_id.board_name, sizeof(board_info_.board_name));
  } else {
#if __x86_64__
    // For x64, we might not find the ZBI_TYPE_PLATFORM_ID, old bootloaders
    // won't support this, for example. If this is the case, cons up the VID/PID
    // here to allow the acpi board driver to load and bind.
    board_info_.vid = PDEV_VID_INTEL;
    board_info_.pid = PDEV_PID_X86;
#else
    zxlogf(ERROR, "platform_bus: ZBI_TYPE_PLATFORM_ID not found");
    return ZX_ERR_INTERNAL;
#endif
  }

  // Set default board_revision.
  zbi_board_info_t zbi_board_info = {};
  GetBoardInfo(&zbi_board_info);
  board_info_.board_revision = zbi_board_info.revision;

  // Then we attach the platform-bus device below it.
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, board_info_.vid},
      {BIND_PLATFORM_DEV_PID, 0, board_info_.pid},
  };
  return DdkAdd(ddk::DeviceAddArgs("platform").set_props(props));
}

void PlatformBus::DdkInit(ddk::InitTxn txn) {
  fbl::Array<uint8_t> board_data;
  zx_status_t status = GetBootItem(ZBI_TYPE_DRV_BOARD_PRIVATE, 0, &board_data);
  if (status != ZX_OK) {
    return txn.Reply(status);
  }
  if (board_data) {
    status = DdkAddMetadata(DEVICE_METADATA_BOARD_PRIVATE, board_data.data(), board_data.size());
    if (status != ZX_OK) {
      return txn.Reply(status);
    }
  }
  return txn.Reply(ZX_OK);  // This will make the device visible and able to be unbound.
}

zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                                zx_handle_t handle) {
  return platform_bus::PlatformBus::Create(parent, name, zx::channel(handle));
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.create = platform_bus_create;
  return ops;
}();

}  // namespace platform_bus

ZIRCON_DRIVER(platform_bus, platform_bus::driver_ops, "zircon", "0.1");
