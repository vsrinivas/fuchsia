// Copyright (c) 2022 The Fuchsia Authors
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_H_

#include <fuchsia/hardware/wlanphyimpl/cpp/banjo.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/device.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

#include <ddktl/device.h>
#include <wlan/drivers/components/network_device.h>

namespace wlan {
namespace nxpfmac {

class Device;
class DeviceInspect;
class WlanInterface;

using DeviceType = ::ddk::Device<Device, ddk::Initializable, ddk::Suspendable>;

class Device : public DeviceType,
               public ::ddk::WlanphyImplProtocol<Device, ::ddk::base_protocol>,
               public ::wlan::drivers::components::NetworkDevice::Callbacks {
 public:
  virtual ~Device();

  // State accessors.

  virtual async_dispatcher_t* GetDispatcher() = 0;

  // ::ddk::Device implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);

  // WlanphyImpl interface implementation.
  zx_status_t WlanphyImplGetSupportedMacRoles(
      wlan_mac_role_t out_supported_mac_roles_list[fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES],
      uint8_t* out_supported_mac_roles_count);
  zx_status_t WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                     uint16_t* out_iface_id);
  zx_status_t WlanphyImplDestroyIface(uint16_t iface_id);
  zx_status_t WlanphyImplSetCountry(const wlanphy_country_t* country);
  zx_status_t WlanphyImplClearCountry();
  zx_status_t WlanphyImplGetCountry(wlanphy_country_t* out_country);
  zx_status_t WlanphyImplSetPsMode(const wlanphy_ps_mode_t* ps_mode);
  zx_status_t WlanphyImplGetPsMode(wlanphy_ps_mode_t* out_ps_mode);

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

  // Helpers
  void DestroyAllIfaces(void);

 protected:
  explicit Device(zx_device_t* parent);

  // This will be called when the driver is being shut down, for example during a reboot, power off
  // or suspend. It is NOT called as part of destruction of the device object (because calling
  // virtual methods in destructors is unreliable). The device may be subject to multiple stages of
  // shutdown, it is therefore possible for shutdown to be called multiple times. The device object
  // may also be destructed after this as a result of the driver framework calling release. Take
  // care that multiple shutdowns or a shutdown followed by destruction does not result in double
  // freeing memory or resources. Because this Device class is not Resumable there is no need to
  // worry about coming back from a shutdown state, it's irreversible.
  virtual void Shutdown() = 0;

 private:
};

}  // namespace nxpfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_H_
