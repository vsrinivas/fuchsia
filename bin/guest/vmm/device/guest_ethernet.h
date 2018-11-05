// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_GUEST_ETHERNET_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_GUEST_ETHERNET_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <zircon/ethernet/cpp/fidl.h>

// Interface for GuestEthernet to send a packet to the guest.
struct GuestEthernetReceiver {
  virtual void Receive(uintptr_t addr, size_t length,
                       const zircon::ethernet::FifoEntry& entry) = 0;
};

class GuestEthernet : public zircon::ethernet::Device {
 public:
  using QueueTxFn = fit::function<zx_status_t(
      uintptr_t addr, size_t length, const zircon::ethernet::FifoEntry& entry)>;

  GuestEthernet(GuestEthernetReceiver* receiver)
      : tx_fifo_wait_(this), receiver_(receiver) {}

  // Interface for the virtio-net device to send a received packet to the host
  // netstack.
  zx_status_t Send(void* offset, size_t length);

  // Interface for the virtio-net device to inform the netstack that a packet
  // has finished being transmitted.
  void Complete(const zircon::ethernet::FifoEntry& entry);

  void OnTxFifoReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal);

  // |zircon::ethernet::Device|
  void GetInfo(GetInfoCallback callback) override;

  // |zircon::ethernet::Device|
  void GetFifos(GetFifosCallback callback) override;

  // |zircon::ethernet::Device|
  void SetIOBuffer(zx::vmo h, SetIOBufferCallback callback) override;

  // |zircon::ethernet::Device|
  void Start(StartCallback callback) override;

  // |zircon::ethernet::Device|
  void Stop(StopCallback callback) override;

  // |zircon::ethernet::Device|
  void ListenStart(ListenStartCallback callback) override;

  // |zircon::ethernet::Device|
  void ListenStop(ListenStopCallback callback) override;

  // |zircon::ethernet::Device|
  void SetClientName(fidl::StringPtr name,
                     SetClientNameCallback callback) override;

  // |zircon::ethernet::Device|
  void GetStatus(GetStatusCallback callback) override;

  // |zircon::ethernet::Device|
  void SetPromiscuousMode(bool enabled,
                          SetPromiscuousModeCallback callback) override;

  // |zircon::ethernet::Device|
  void ConfigMulticastAddMac(zircon::ethernet::MacAddress addr,
                             ConfigMulticastAddMacCallback callback) override;

  // |zircon::ethernet::Device|
  void ConfigMulticastDeleteMac(
      zircon::ethernet::MacAddress addr,
      ConfigMulticastDeleteMacCallback callback) override;

  // |zircon::ethernet::Device|
  void ConfigMulticastSetPromiscuousMode(
      bool enabled,
      ConfigMulticastSetPromiscuousModeCallback callback) override;

  // |zircon::ethernet::Device|
  void ConfigMulticastTestFilter(
      ConfigMulticastTestFilterCallback callback) override;

  // |zircon::ethernet::Device|
  void DumpRegisters(DumpRegistersCallback callback) override;

 private:
  static constexpr uint16_t kVirtioNetQueueSize = 256;
  zx::fifo tx_fifo_;
  zx::fifo rx_fifo_;

  zx::vmo io_vmo_;
  uintptr_t io_addr_;
  size_t io_size_;

  std::vector<zircon::ethernet::FifoEntry> rx_entries_ =
      std::vector<zircon::ethernet::FifoEntry>(kVirtioNetQueueSize);
  size_t rx_entries_count_ = 0;

  async::WaitMethod<GuestEthernet, &GuestEthernet::OnTxFifoReadable>
      tx_fifo_wait_;

  GuestEthernetReceiver* receiver_;
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_GUEST_ETHERNET_H_