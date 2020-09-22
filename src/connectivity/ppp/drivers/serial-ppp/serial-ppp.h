// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_PPP_DRIVERS_SERIAL_PPP_SERIAL_PPP_H_
#define SRC_CONNECTIVITY_PPP_DRIVERS_SERIAL_PPP_SERIAL_PPP_H_

#include <fuchsia/hardware/network/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zx/channel.h>
#include <lib/zx/port.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <cstddef>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/network/device.h>
#include <ddktl/protocol/serial.h>
#include <fbl/auto_lock.h>
#include <fbl/span.h>

#include "lib/common/ppp.h"
#include "lib/hdlc/frame.h"
#include "src/lib/vmo_store/vmo_store.h"

namespace ppp {

class SerialPpp;
using DeviceType = ddk::Device<SerialPpp>;

class SerialPpp final : public DeviceType,
                        public ddk::NetworkDeviceImplProtocol<SerialPpp>,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_NETWORK_DEVICE_IMPL> {
 public:
  static constexpr zx::duration kSerialTimeout = zx::msec(10);
  // FIFO depth is abitrarily low since PPP is quite slow. There's no point in having many buffers.
  static constexpr uint16_t kFifoDepth = 64;

  explicit SerialPpp(zx_device_t* parent);
  SerialPpp(zx_device_t* parent, ddk::SerialProtocolClient serial);

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // ddk::Releasable
  void DdkRelease();

  // ddk::Unbindable
  void DdkUnbind(ddk::UnbindTxn txn);

  // ddk::NetworkDeviceImplProtocol
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplGetStatus(status_t* out_status);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count);
  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo);
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id);
  void NetworkDeviceImplSetSnoop(bool snoop);

  // Used in tests:
  void set_enable_rx_timeout(bool enable) { enable_rx_timeout_ = enable; }

  // Synchronously shuts down |WorkerLoop| thread if one is running and disposes of serial
  // connection. Does nothing otherwise.
  void Shutdown();

 private:
  static constexpr uint16_t kDefaultMtu = 1500;
  // Max buffer size selected to fit body of two messages plus single header/footer padding.
  static constexpr size_t kMaxBufferSize = 2 * kDefaultMtu + 8;

  void WorkerLoop();

  // Reads data from the |serial_| socket, finds frame boundaries and publishes parsed frames to the
  // network device interface.
  //
  // If |fetch_from_socket| is trued, more data will be read from the |serial_| socket. Only local
  // |rx_frame_buffer_| is considered otherwise.
  //
  // Returns |true| if we should wait for more |serial_| to become readable.
  bool ConsumeSerial(bool fetch_from_socket);

  zx_status_t WriteFramed(FrameView frame);

  ddk::SerialProtocolClient serial_protocol_;
  ddk::NetworkDeviceIfcProtocolClient netdevice_protocol_;

  struct PendingBuffer {
    uint32_t id;
    fbl::Span<uint8_t> data;
    uint8_t type;
  };

  bool enable_rx_timeout_ = true;

  // State lock must be acquired for lifecycle events.
  fbl::Mutex state_lock_;
  std::thread thread_ __TA_GUARDED(state_lock_);
  // NB: |serial_| and |port_| are not marked as guarded so they can be accessed without locking
  // |state_lock_| in |WorkerLoop|. Safety comes from |serial_| and |port_| only being read from the
  // |WorkerLoop| thread and only being modified after the thread is joined in lifecycle events.
  zx::socket serial_;
  zx::port port_;

  fbl::Mutex rx_lock_;
  std::queue<PendingBuffer> rx_space_ __TA_GUARDED(rx_lock_);
  fbl::Mutex tx_lock_;
  std::queue<PendingBuffer> pending_tx_ __TA_GUARDED(tx_lock_);
  vmo_store::VmoStore<vmo_store::SlabStorage<uint8_t>> vmos_;

  // Buffer to accumulate rx frames, to be used only in |WorkerLoop| thread.
  std::array<uint8_t, kMaxBufferSize> rx_frame_buffer_;
  decltype(rx_frame_buffer_)::iterator rx_frame_buffer_offset_;
};

}  // namespace ppp

#endif  // SRC_CONNECTIVITY_PPP_DRIVERS_SERIAL_PPP_SERIAL_PPP_H_
