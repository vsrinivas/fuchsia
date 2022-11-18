// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_

#include <fidl/fuchsia.factory.wlan/cpp/wire.h>
#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/driver/wire.h>
#include <lib/ddk/device.h>
#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/channel_read.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fit/function.h>
#include <lib/sync/cpp/completion.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

#include <ddktl/device.h>
#include <wlan/drivers/components/network_device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"

namespace wlan {
namespace brcmfmac {

class Device;
class DeviceInspect;
class WlanInterface;
using DeviceType =
    ::ddk::Device<Device, ddk::Initializable, ddk::Messageable<fuchsia_factory_wlan::Iovar>::Mixin,
                  ddk::Suspendable, ddk::ServiceConnectable>;
class Device : public DeviceType,
               public fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl>,
               public ::wlan::drivers::components::NetworkDevice::Callbacks {
 public:
  virtual ~Device();

  // State accessors
  brcmf_pub* drvr();
  const brcmf_pub* drvr() const;
  ::wlan::drivers::components::NetworkDevice& NetDev() { return network_device_; }

  // Virtual state accessors
  virtual async_dispatcher_t* GetDispatcher() = 0;
  virtual DeviceInspect* GetInspect() = 0;
  virtual bool IsNetworkDeviceBus() const = 0;

  // ::ddk::Device implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);
  zx_status_t DdkServiceConnect(const char* service_name, fdf::Channel channel);

  // WlanphyImpl interface implementation.
  void GetSupportedMacRoles(fdf::Arena& arena,
                            GetSupportedMacRolesCompleter::Sync& completer) override;
  void CreateIface(CreateIfaceRequestView request, fdf::Arena& arena,
                   CreateIfaceCompleter::Sync& completer) override;
  void DestroyIface(DestroyIfaceRequestView request, fdf::Arena& arena,
                    DestroyIfaceCompleter::Sync& completer) override;
  void SetCountry(SetCountryRequestView request, fdf::Arena& arena,
                  SetCountryCompleter::Sync& completer) override;
  void GetCountry(fdf::Arena& arena, GetCountryCompleter::Sync& completer) override;
  void ClearCountry(fdf::Arena& arena, ClearCountryCompleter::Sync& completer) override;
  void SetPsMode(SetPsModeRequestView request, fdf::Arena& arena,
                 SetPsModeCompleter::Sync& completer) override;
  void GetPsMode(fdf::Arena& arena, GetPsModeCompleter::Sync& completer) override;

  // NetworkDevice::Callbacks implementation
  zx_status_t NetDevInit() override;
  void NetDevRelease() override;
  void NetDevStart(wlan::drivers::components::NetworkDevice::Callbacks::StartTxn txn) override;
  void NetDevStop(wlan::drivers::components::NetworkDevice::Callbacks::StopTxn txn) override;
  void NetDevGetInfo(device_info_t* out_info) override;
  void NetDevQueueTx(cpp20::span<wlan::drivers::components::Frame> frames) override;
  void NetDevQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count,
                          uint8_t* vmo_addrs[]) override;
  zx_status_t NetDevPrepareVmo(uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_address,
                               size_t mapped_size) override;
  void NetDevReleaseVmo(uint8_t vmo_id) override;
  void NetDevSetSnoopEnabled(bool snoop) override;

  // Trampolines for DDK functions, for platforms that support them.
  virtual zx_status_t Init() = 0;
  virtual zx_status_t DeviceAdd(device_add_args_t* args, zx_device_t** out_device) = 0;
  virtual void DeviceAsyncRemove(zx_device_t* dev) = 0;
  virtual zx_status_t LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) = 0;
  virtual zx_status_t DeviceGetMetadata(uint32_t type, void* buf, size_t buflen,
                                        size_t* actual) = 0;

  // Helper
  void DestroyAllIfaces(void);

 protected:
  // Only derived classes are allowed to create this object.
  explicit Device(zx_device_t* parent);

  // This will be called when the driver is being shut down, for example during a reboot, power off
  // or suspend. It is NOT called as part of destruction of the device object (because calling
  // virtual methods in destructors is unreliable). The device may be subject to multiple stages of
  // shutdown, it is therefore possible for shutdown to be called multiple times. The device object
  // may also be destructed after this as a result of the driver framework calling release. Take
  // care that a multiple shutdowns or a shutdown followed by destruction does not result in double
  // freeing memory or resources. Because this Device class is not Resumable there is no need to
  // worry about coming back from a shutdown state, it's irreversible.
  virtual void Shutdown() = 0;

  // Dispatcher for the FIDL server
  fdf::Dispatcher dispatcher_;
  libsync::Completion completion_;

 private:
  std::unique_ptr<brcmf_pub> brcmf_pub_;
  std::mutex lock_;

  // Two fixed interfaces supported;  the default interface as a client, and a second one as an AP.
  WlanInterface* client_interface_;
  WlanInterface* ap_interface_;

  ::wlan::drivers::components::NetworkDevice network_device_;

  // fidl::WireServer<fuchsia_factory_wlan_iovar::Iovar> Implementation
  void Get(GetRequestView request, GetCompleter::Sync& _completer) override;
  void Set(SetRequestView request, SetCompleter::Sync& _completer) override;

  // Helpers
  void ShutdownDispatcher();
  void DestroyIface(WlanInterface** iface_ptr, fit::callback<void(zx_status_t)> respond);
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_
