// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_BUS_H_
#define SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_BUS_H_

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <fuchsia/hardware/iommu/cpp/banjo.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/device.h>
#include <lib/driver2/outgoing_directory.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/iommu.h>
#include <lib/zx/resource.h>
#include <lib/zx/status.h>
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
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <sdk/lib/sys/component/cpp/outgoing_directory.h>

#include "platform-device.h"
#include "proxy-protocol.h"

namespace platform_bus {

class PlatformBus;
using PlatformBusType = ddk::Device<PlatformBus, ddk::GetProtocolable, ddk::Initializable,
                                    ddk::Messageable<fuchsia_sysinfo::SysInfo>::Mixin>;

// This is the main class for the platform bus driver.
class PlatformBus : public PlatformBusType,
                    public fdf::WireServer<fuchsia_hardware_platform_bus::PlatformBus>,
                    public ddk::IommuProtocol<PlatformBus> {
 public:
  static zx_status_t Create(zx_device_t* parent, const char* name, zx::channel items_svc);

  PlatformBus(zx_device_t* parent, zx::channel items_svc);

  // Device protocol implementation.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // fuchsia.hardware.platform.bus.PlatformBus implementation.
  void NodeAdd(NodeAddRequestView request, fdf::Arena& arena,
               NodeAddCompleter::Sync& completer) override;
  void ProtocolNodeAdd(ProtocolNodeAddRequestView request, fdf::Arena& arena,
                       ProtocolNodeAddCompleter::Sync& completer) override;
  void RegisterProtocol(RegisterProtocolRequestView request, fdf::Arena& arena,
                        RegisterProtocolCompleter::Sync& completer) override;

  void GetBoardInfo(fdf::Arena& arena, GetBoardInfoCompleter::Sync& completer) override;
  void SetBoardInfo(SetBoardInfoRequestView request, fdf::Arena& arena,
                    SetBoardInfoCompleter::Sync& completer) override;
  void SetBootloaderInfo(SetBootloaderInfoRequestView request, fdf::Arena& arena,
                         SetBootloaderInfoCompleter::Sync& completer) override;

  void RegisterSysSuspendCallback(RegisterSysSuspendCallbackRequestView request, fdf::Arena& arena,
                                  RegisterSysSuspendCallbackCompleter::Sync& completer) override;
  void AddComposite(AddCompositeRequestView request, fdf::Arena& arena,
                    AddCompositeCompleter::Sync& completer) override;
  void AddCompositeImplicitPbusFragment(
      AddCompositeImplicitPbusFragmentRequestView request, fdf::Arena& arena,
      AddCompositeImplicitPbusFragmentCompleter::Sync& completer) override;

  // SysInfo protocol implementation.
  void GetBoardName(GetBoardNameCompleter::Sync& completer) override;
  void GetBoardRevision(GetBoardRevisionCompleter::Sync& completer) override;
  void GetBootloaderVendor(GetBootloaderVendorCompleter::Sync& completer) override;
  void GetInterruptControllerInfo(GetInterruptControllerInfoCompleter::Sync& completer) override;

  // IOMMU protocol implementation.
  zx_status_t IommuGetBti(uint32_t iommu_index, uint32_t bti_id, zx::bti* out_bti);

  // Returns the resource handle to be used for creating MMIO regions, IRQs, and SMC ranges.
  // Currently this just returns the root resource, but we may change this to a more
  // limited resource in the future.
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource GetResource() const { return zx::unowned_resource(get_root_resource()); }

  struct BootItemResult {
    zx::vmo vmo;
    uint32_t length;
  };
  // Returns ZX_ERR_NOT_FOUND when boot item wasn't found.
  zx::status<BootItemResult> GetBootItem(uint32_t type, uint32_t extra);
  zx::status<fbl::Array<uint8_t>> GetBootItemArray(uint32_t type, uint32_t extra);

  inline ddk::GpioImplProtocolClient* gpio() { return &*gpio_; }

  fdf::WireClient<fuchsia_hardware_platform_bus::SysSuspend>& suspend_cb() { return suspend_cb_; }

  fuchsia_hardware_platform_bus::TemporaryBoardInfo board_info() {
    fbl::AutoLock lock(&board_info_lock_);
    return board_info_;
  }

  driver::OutgoingDirectory& outgoing() { return outgoing_; }

  fdf::UnownedDispatcher dispatcher() { return dispatcher_->borrow(); }

 private:
  fdf::WireClient<fuchsia_hardware_platform_bus::SysSuspend> suspend_cb_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformBus);

  zx::status<zbi_board_info_t> GetBoardInfo();
  zx_status_t Init();

  zx::status<> NodeAddInternal(fuchsia_hardware_platform_bus::Node& node);
  zx::status<> ValidateResources(fuchsia_hardware_platform_bus::Node& node);

  fidl::ClientEnd<fuchsia_boot::Items> items_svc_;

  // Protects board_name_completer_.
  fbl::Mutex board_info_lock_;
  fuchsia_hardware_platform_bus::TemporaryBoardInfo board_info_ __TA_GUARDED(board_info_lock_) = {};
  // List to cache requests when board_name is not yet set.
  std::vector<GetBoardNameCompleter::Async> board_name_completer_ __TA_GUARDED(board_info_lock_);

  fbl::Mutex bootloader_info_lock_;
  fuchsia_hardware_platform_bus::BootloaderInfo bootloader_info_
      __TA_GUARDED(bootloader_info_lock_) = {};
  // List to cache requests when vendor is not yet set.
  std::vector<GetBootloaderVendorCompleter::Async> bootloader_vendor_completer_
      __TA_GUARDED(bootloader_info_lock_);

  fuchsia_sysinfo::wire::InterruptControllerType interrupt_controller_type_ =
      fuchsia_sysinfo::wire::InterruptControllerType::kUnknown;

  // Protocols that are optionally provided by the board driver.
  std::optional<ddk::ClockImplProtocolClient> clock_;
  std::optional<ddk::GpioImplProtocolClient> gpio_;
  std::optional<ddk::IommuProtocolClient> iommu_;

  struct ProtoReadyResponse {
    fdf::Arena arena;
    ProtocolNodeAddCompleter::Async completer;
    std::unique_ptr<async::Task> timeout_task;
  };
  // Key is the protocol ID (i.e. ZX_PROTOCOL_GPIO, ZX_PROTOCOL_CLOCK, etc).
  std::unordered_map<uint32_t, ProtoReadyResponse> proto_ready_responders_
      __TA_GUARDED(proto_completion_mutex_);
  // Protects proto_ready_responders_.
  fbl::Mutex proto_completion_mutex_;

  // Dummy IOMMU.
  zx::iommu iommu_handle_;

  std::map<std::pair<uint32_t, uint32_t>, zx::bti> cached_btis_;

  zx_device_t* protocol_passthrough_ = nullptr;
  driver::OutgoingDirectory outgoing_;
  fdf::UnownedDispatcher dispatcher_;
};

}  // namespace platform_bus

__BEGIN_CDECLS
zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                                zx_handle_t rpc_channel);
__END_CDECLS

#endif  // SRC_DEVICES_BUS_DRIVERS_PLATFORM_PLATFORM_BUS_H_
