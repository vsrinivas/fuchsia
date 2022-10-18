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
#include <fuchsia/hardware/network/mac/cpp/banjo.h>
#include <zircon/types.h>

#include <mutex>

#include <ddktl/device.h>

#include "igc_api.h"
#include "src/connectivity/network/drivers/network-device/device/public/locks.h"
#include "src/lib/vmo_store/vmo_store.h"

namespace ethernet {
namespace igc {

using VmoStore = vmo_store::VmoStore<vmo_store::SlabStorage<uint32_t>>;

// Internal Rx/Tx buffer metadata.
constexpr static size_t kEthRxBufCount =
    256;  // Depth of Rx buffer, equal to the length of Rx desc ring buffer.
constexpr static size_t kEthRxBufSize = 2048;  // Size of each Rx frame buffer.
constexpr static size_t kEthRxDescSize = 16;

constexpr static size_t kEthTxDescRingCount =
    256;  // The length of Tx desc ring buffer pool, should be greater than the actual tx buffer
          // count, which left dummy spaces for ring buffer indexing. Both tx desc ring buffer pool
          // length and actual tx buffer count should be multiples of 2.

constexpr static size_t kEthTxBufCount =
    128;  // Depth of Tx buffer that driver reports to network device.
constexpr static size_t kEthTxBufSize = 2048;  // Size of each Tx frame buffer.
constexpr static size_t kEthTxDescSize = 16;

class IgcDriver : public ::ddk::NetworkDeviceImplProtocol<IgcDriver>,
                  public ::ddk::NetworkPortProtocol<IgcDriver>,
                  public ::ddk::MacAddrProtocol<IgcDriver> {
 public:
  struct buffer_info;
  struct adapter;

  explicit IgcDriver(zx_device_t* parent);
  ~IgcDriver();

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

  // NetworkPort protocol implementation.
  void NetworkPortGetInfo(port_info_t* out_info);
  void NetworkPortGetStatus(port_status_t* out_status);
  void NetworkPortSetActive(bool active);
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc);
  void NetworkPortRemoved();

  // MacAddr protocol:
  void MacAddrGetAddress(uint8_t* out_mac);
  void MacAddrGetFeatures(features_t* out_features);
  void MacAddrSetMode(mode_t mode, const uint8_t* multicast_macs_list, size_t multicast_macs_count);

  buffer_info* RxBuffer() { return rx_buffers_; }
  std::shared_ptr<adapter> Adapter() { return adapter_; }
  static int IgcIrqThreadFunc(void* arg);

  // The return value indicates whether the online status has been changed.
  bool OnlineStatusUpdate();

  struct adapter {
    struct igc_hw hw;
    struct igc_osdep osdep;
    mtx_t lock;
    zx_device_t* zxdev;
    thrd_t thread;
    zx_handle_t irqh;
    zx_handle_t btih;

    // Buffer to store tx/rx descriptor rings.
    io_buffer_t desc_buffer;

    // tx/rx descriptor rings
    struct igc_tx_desc* txdr;
    union igc_adv_rx_desc* rxdr;

    // Base physical addresses for tx/rx rings.
    // Store as 64-bit integer to match hw registers sizes.
    uint64_t txd_phys;
    uint64_t rxd_phys;

    // callback interface to attached ethernet layer
    ::ddk::NetworkDeviceIfcProtocolClient netdev_ifc;

    uint32_t txh_ind;  // Index of the head of awaiting(for the adapter to pick up) buffers in txdr.
    uint32_t txt_ind;  // Index of the tail of awaiting(for the adapter to pick up) buffers in txdr.
    uint32_t rxh_ind;  // Index of the head of available buffers in rxdr.
    uint32_t rxt_ind;  // Index of the tail of available buffers in rxdr.

    // Protect the rx data path between the two operations: QueueRxSpace and IgcIrqThreadFunc.
    std::mutex rx_lock;
    // Protect the tx data path between the two operations: QueueTx and ReapTxBuffers.
    std::mutex tx_lock;

    mmio_buffer_t bar0_mmio;

    pci_interrupt_mode_t irq_mode;
  };

  // We store the buffer information for each rx_space_buffer in this struct, so that we can do
  // CompleteRx based on these information.
  struct buffer_info {
    uint32_t buffer_id;
    bool available;  // Indicate whether this buffer info maps to a buffer passed down through
                     // RxSpace.
  };

  // Status of hw.
  bool online_ = false;

 private:
  bool IsValidEthernetAddr(uint8_t* addr);
  void IdentifyHardware();
  zx_status_t AllocatePCIResources();
  zx_status_t InitBuffer();
  void InitTransmitUnit();
  void InitReceiveUnit();
  void IfSetPromisc(uint32_t flags);
  void ReapTxBuffers();

  zx_device_t* parent_ = nullptr;
  zx_device_t* device_ = nullptr;

  std::shared_ptr<struct adapter> adapter_;

  std::mutex started_mutex_;
  std::mutex online_mutex_;
  bool started_ __TA_GUARDED(started_mutex_) = false;

  network_device_impl_protocol_t netdev_impl_proto_;

  // Lock for VMO changes.
  network::SharedLock vmo_lock_;

  // Note: Extract Tx/Rx related variables to pcie_txrx.{h,cc}.
  // The io_buffers created from the pre-allocated VMOs.
  std::unique_ptr<VmoStore> vmo_store_ __TA_GUARDED(vmo_lock_);

  // An extension for rx decriptor.
  buffer_info rx_buffers_[kEthRxBufCount];

  // An extension for tx decriptor.
  buffer_info tx_buffers_[kEthTxBufCount];
};

}  // namespace igc
}  // namespace ethernet

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_THIRD_PARTY_IGC_IGC_DRIVER_H_
