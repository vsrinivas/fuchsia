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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DATA_PLANE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DATA_PLANE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>

#include <memory>

#include <wlan/drivers/components/frame_storage.h>
#include <wlan/drivers/components/network_device.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/bus_interface.h"

namespace wlan::nxpfmac {

// An interface used by the data plane to communicate certain events that require processing outside
// of the regular data flow.
class DataPlaneIfc {
 public:
  virtual ~DataPlaneIfc();

  // The data plane completed a transmission attempt of an EAPOL frame, successful or not.
  virtual void OnEapolTransmitted(wlan::drivers::components::Frame&& frame, zx_status_t status) = 0;
  // The data plane received an EAPOL frame.
  virtual void OnEapolReceived(wlan::drivers::components::Frame&& frame) = 0;
};

class DataPlane : public wlan::drivers::components::NetworkDevice::Callbacks {
 public:
  ~DataPlane() override;
  static zx_status_t Create(zx_device_t* parent, DataPlaneIfc* ifc, BusInterface* bus,
                            void* mlan_adapter, std::unique_ptr<DataPlane>* out_data_plane);

  network_device_ifc_protocol_t NetDevIfcProto();
  void DeferRxWork();

  void CompleteTx(wlan::drivers::components::Frame&& frame, zx_status_t status);
  void CompleteRx(wlan::drivers::components::Frame&& frame);
  std::optional<wlan::drivers::components::Frame> AcquireFrame();

  // NetworkDevice::Callbacks implementation
  void NetDevRelease() override;
  zx_status_t NetDevInit() override;
  void NetDevStart(StartTxn txn) override;
  void NetDevStop(StopTxn txn) override;
  void NetDevGetInfo(device_info_t* out_info) override;
  void NetDevQueueTx(cpp20::span<wlan::drivers::components::Frame> frames) override;
  void NetDevQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count,
                          uint8_t* vmo_addrs[]) override;
  zx_status_t NetDevPrepareVmo(uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_address,
                               size_t mapped_size) override;
  void NetDevReleaseVmo(uint8_t vmo_id) override;
  void NetDevSetSnoopEnabled(bool snoop) override;

 private:
  explicit DataPlane(zx_device_t* parent, DataPlaneIfc* ifc, BusInterface* bus, void* mlan_adapter);
  zx_status_t Init();

  DataPlaneIfc* ifc_ = nullptr;

  ::wlan::drivers::components::NetworkDevice network_device_;
  sync_completion_t network_device_released_;
  wlan::drivers::components::FrameStorage rx_frames_;
  async::Loop rx_work_loop_{&kAsyncLoopConfigNeverAttachToThread};

  BusInterface* bus_;
  void* mlan_adapter_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DATA_PLANE_H_
