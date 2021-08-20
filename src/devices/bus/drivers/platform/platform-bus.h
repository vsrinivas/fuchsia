// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_BUS_H_

#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/iommu/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/hardware/powerimpl/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/ddk/device.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/iommu.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

#include <map>
#include <optional>
#include <vector>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "platform-device.h"
#include "proxy-protocol.h"

namespace platform_bus {

class PlatformBus;
using PlatformBusType = ddk::Device<PlatformBus, ddk::GetProtocolable, ddk::Initializable,
                                    ddk::Messageable<fuchsia_sysinfo::SysInfo>::Mixin>;

// This is the main class for the platform bus driver.
class PlatformBus : public PlatformBusType,
                    public ddk::PBusProtocol<PlatformBus, ddk::base_protocol>,
                    public ddk::IommuProtocol<PlatformBus> {
 public:
  static zx_status_t Create(zx_device_t* parent, const char* name, zx::channel items_svc);

  PlatformBus(zx_device_t* parent, zx::channel items_svc);

  // Device protocol implementation.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // Platform bus protocol implementation.
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev);
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev);
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol,
                                   size_t protocol_size);
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info);
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info);
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info);
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev,
                                     /* const device_fragment_t* */ uint64_t fragments_list,
                                     size_t fragments_count, const char* primary_fragment);
  zx_status_t PBusAddComposite(const pbus_dev_t* dev,
                               /* const device_fragment_t* */ uint64_t fragments,
                               size_t fragment_count, const char* primary_fragment);

  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin);

  // SysInfo protocol implementation.
  void GetBoardName(GetBoardNameRequestView request,
                    GetBoardNameCompleter::Sync& completer) override;
  void GetBoardRevision(GetBoardRevisionRequestView request,
                        GetBoardRevisionCompleter::Sync& completer) override;
  void GetBootloaderVendor(GetBootloaderVendorRequestView request,
                           GetBootloaderVendorCompleter::Sync& completer) override;
  void GetInterruptControllerInfo(GetInterruptControllerInfoRequestView request,
                                  GetInterruptControllerInfoCompleter::Sync& completer) override;

  // IOMMU protocol implementation.
  zx_status_t IommuGetBti(uint32_t iommu_index, uint32_t bti_id, zx::bti* out_bti);

  // Returns the resource handle to be used for creating MMIO regions, IRQs, and SMC ranges.
  // Currently this just returns the root resource, but we may change this to a more
  // limited resource in the future.
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource GetResource() const { return zx::unowned_resource(get_root_resource()); }

  zx_status_t GetBootItem(uint32_t type, uint32_t extra, zx::vmo* vmo, uint32_t* length);
  zx_status_t GetBootItem(uint32_t type, uint32_t extra, fbl::Array<uint8_t>* out);

  inline ddk::GpioImplProtocolClient* gpio() { return &*gpio_; }

  pbus_sys_suspend_t suspend_cb() { return suspend_cb_; }

 private:
  pbus_sys_suspend_t suspend_cb_ = {};

  DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformBus);

  zx_status_t GetBoardInfo(zbi_board_info_t* board_info);
  zx_status_t Init();

  zx::channel items_svc_;

  // Protects board_name_completer_.
  fbl::Mutex board_info_lock_;
  pdev_board_info_t board_info_ __TA_GUARDED(board_info_lock_) = {};
  // List to cache requests when board_name is not yet set.
  std::vector<GetBoardNameCompleter::Async> board_name_completer_ __TA_GUARDED(board_info_lock_);

  fbl::Mutex bootloader_info_lock_;
  pbus_bootloader_info_t bootloader_info_ __TA_GUARDED(bootloader_info_lock_) = {};
  // List to cache requests when vendor is not yet set.
  std::vector<GetBootloaderVendorCompleter::Async> bootloader_vendor_completer_
      __TA_GUARDED(bootloader_info_lock_);

  fuchsia_sysinfo::wire::InterruptControllerType interrupt_controller_type_ =
      fuchsia_sysinfo::wire::InterruptControllerType::kUnknown;

  // Protocols that are optionally provided by the board driver.
  std::optional<ddk::ClockImplProtocolClient> clock_;
  std::optional<ddk::GpioImplProtocolClient> gpio_;
  std::optional<ddk::IommuProtocolClient> iommu_;
  std::optional<ddk::PowerImplProtocolClient> power_;
  std::optional<ddk::SysmemProtocolClient> sysmem_;

  // Completion used by WaitProtocol().
  sync_completion_t proto_completion_ __TA_GUARDED(proto_completion_mutex_);
  // Protects proto_completion_.
  fbl::Mutex proto_completion_mutex_;

  // Dummy IOMMU.
  zx::iommu iommu_handle_;

  std::map<std::pair<uint32_t, uint32_t>, zx::bti> cached_btis_;
};

}  // namespace platform_bus

__BEGIN_CDECLS
zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                                zx_handle_t rpc_channel);
__END_CDECLS

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_BUS_H_
