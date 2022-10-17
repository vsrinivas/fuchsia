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

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_DRIVER_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_DRIVER_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <zircon/types.h>

#include <mutex>

#include <ddktl/device.h>

namespace ethernet {
namespace igc {

class IgcDevice : public ::ddk::NetworkDeviceImplProtocol<IgcDevice> {
 public:
  explicit IgcDevice(zx_device_t* parent);

  // Called by DDK.
  void Release();

  zx_status_t Init();

  // NetworkDeviceImpl implementation
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie)
      __TA_EXCLUDES(started_mutex_);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie)
      __TA_EXCLUDES(started_mutex_);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count)
      __TA_EXCLUDES(started_mutex_);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback, void* cookie);
  void NetworkDeviceImplReleaseVmo(uint8_t id);
  void NetworkDeviceImplSetSnoop(bool snoop);

 private:
  zx_device_t* parent_ = nullptr;
  zx_device_t* device_ = nullptr;

  std::mutex started_mutex_;
  network_device_impl_protocol_t netdev_impl_proto_;
  // The protocol call into network device.
  ::ddk::NetworkDeviceIfcProtocolClient netdev_ifc_;
};

}  // namespace igc
}  // namespace ethernet

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_DRIVER_H_
