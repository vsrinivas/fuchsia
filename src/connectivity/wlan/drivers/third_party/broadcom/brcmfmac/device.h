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

#include <fuchsia/factory/wlan/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/wlanphyimpl.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"

namespace wlan {
namespace brcmfmac {

class Device;
class WlanInterface;

class Device : public ::ddk::Device<Device, ddk::Messageable>,
               ::llcpp::fuchsia::factory::wlan::Iovar::Interface,
               public ::ddk::WlanphyImplProtocol<Device, ::ddk::base_protocol> {
 public:
  virtual ~Device();

  // State accessors.
  brcmf_pub* drvr();
  const brcmf_pub* drvr() const;

  // ::ddk::Device implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // WlanphyImpl interface implementation.
  zx_status_t WlanphyImplQuery(wlanphy_impl_info_t* out_info);
  zx_status_t WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                     uint16_t* out_iface_id);
  zx_status_t WlanphyImplDestroyIface(uint16_t iface_id);
  zx_status_t WlanphyImplSetCountry(const wlanphy_country_t* country);
  zx_status_t WlanphyImplClearCountry();
  zx_status_t WlanphyImplGetCountry(wlanphy_country_t* out_country);

  // Trampolines for DDK functions, for platforms that support them.
  virtual zx_status_t DeviceAdd(device_add_args_t* args, zx_device_t** out_device) = 0;
  virtual void DeviceAsyncRemove(zx_device_t* dev) = 0;
  virtual zx_status_t LoadFirmware(const char* path, zx_handle_t* fw, size_t* size) = 0;
  virtual zx_status_t DeviceGetMetadata(uint32_t type, void* buf, size_t buflen,
                                        size_t* actual) = 0;

 protected:
  explicit Device(zx_device_t* parent);

  // Initialize the device-agnostic bits of the device
  zx_status_t Init();

  void DisableDispatcher();

  std::unique_ptr<brcmf_pub> brcmf_pub_;

 private:
  std::unique_ptr<::async::Loop> dispatcher_;

  // Two fixed interfaces supported; the default instance as a client, and a second one as an AP.
  WlanInterface* client_interface_;
  WlanInterface* ap_interface_;

  // ::llcpp::fuchsia::factory::wlan::iovar::Iovar::Interface Implementation
  void Get(int32_t iface_idx, int32_t cmd, ::fidl::VectorView<uint8_t> request,
           GetCompleter::Sync& _completer) override;
  void Set(int32_t iface_idx, int32_t cmd, ::fidl::VectorView<uint8_t> request,
           SetCompleter::Sync& _completer) override;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_
