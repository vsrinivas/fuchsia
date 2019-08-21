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

#include <lib/async-loop/cpp/loop.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/wlanphyimpl.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"

namespace wlan {
namespace brcmfmac {

// This class uses the DDKTL classes to manage the lifetime of a brcmfmac driver instance.
class Device : public ::ddk::Device<Device, ::ddk::Unbindable>,
               public ::ddk::WlanphyImplProtocol<Device, ::ddk::base_protocol> {
 public:
  // Static factory function for Device instances. This factory does not return an owned instance,
  // as on successful invocation the instance will have its lifecycle maanged by the devhost.
  static zx_status_t Create(zx_device_t* parent_device, Device** device_out,
                            simulation::Environment* env = nullptr);
  explicit Device(std::unique_ptr<::async::Loop> dispatcher, std::unique_ptr<brcmf_pub> brcmf_pub,
                  std::unique_ptr<brcmf_bus> brcmf_bus);

  Device(const Device& device) = delete;
  Device& operator=(const Device& other) = delete;

  // For testing: unbind this instance explicitly, when no devhost is available. After this call,
  // the instance is deleted.
  void SimUnbind();

  // DDK interface implementation.
  void DdkUnbind();
  void DdkRelease();

  // WlanphyImpl protocol implementation.
  zx_status_t WlanphyImplQuery(wlanphy_impl_info_t* out_info);
  zx_status_t WlanphyImplCreateIface(const wlanphy_impl_create_iface_req_t* req,
                                     uint16_t* out_iface_id);
  zx_status_t WlanphyImplDestroyIface(uint16_t iface_id);
  zx_status_t WlanphyImplSetCountry(const wlanphy_country_t* country);

 protected:
  explicit Device(zx_device_t* parent);
  ~Device();

 private:
  std::unique_ptr<::async::Loop> dispatcher_;
  std::unique_ptr<brcmf_pub> brcmf_pub_;
  std::unique_ptr<brcmf_bus> brcmf_bus_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_
