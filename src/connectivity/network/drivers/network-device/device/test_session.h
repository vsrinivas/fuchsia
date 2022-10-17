// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_SESSION_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_SESSION_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/event.h>
#include <zircon/device/network.h>

#include "definitions.h"

namespace network {
namespace testing {

class TestSession {
 public:
  static constexpr uint16_t kDefaultDescriptorCount = 256;
  static constexpr uint64_t kDefaultBufferLength = ZX_PAGE_SIZE / 2;

  TestSession() = default;

  zx_status_t Open(fidl::WireSyncClient<netdev::Device>& netdevice, const char* name,
                   netdev::wire::SessionFlags flags = netdev::wire::SessionFlags::kPrimary,
                   uint16_t num_descriptors = kDefaultDescriptorCount,
                   uint64_t buffer_size = kDefaultBufferLength);

  zx_status_t Init(uint16_t descriptor_count, uint64_t buffer_size);
  zx::result<netdev::wire::SessionInfo> GetInfo();
  void Setup(fidl::ClientEnd<netdev::Session> session, netdev::wire::Fifos fifos);
  [[nodiscard]] zx_status_t AttachPort(netdev::wire::PortId port_id,
                                       std::vector<netdev::wire::FrameType> frame_types);
  [[nodiscard]] zx_status_t DetachPort(netdev::wire::PortId port_id);

  zx_status_t Close();
  zx_status_t WaitClosed(zx::time deadline);
  void ZeroVmo();
  buffer_descriptor_t& ResetDescriptor(uint16_t index);
  buffer_descriptor_t& descriptor(uint16_t index);
  uint8_t* buffer(uint64_t offset);

  zx_status_t FetchRx(uint16_t* descriptors, size_t count, size_t* actual) const;
  zx_status_t FetchTx(uint16_t* descriptors, size_t count, size_t* actual) const;
  zx_status_t SendRx(const uint16_t* descriptor, size_t count, size_t* actual) const;
  zx_status_t SendTx(const uint16_t* descriptor, size_t count, size_t* actual) const;
  zx_status_t SendTxData(const netdev::wire::PortId& port_id, uint16_t descriptor_index,
                         const std::vector<uint8_t>& data);

  zx_status_t FetchRx(uint16_t* descriptor) const {
    size_t actual;
    return FetchRx(descriptor, 1, &actual);
  }

  zx_status_t FetchTx(uint16_t* descriptor) const {
    size_t actual;
    return FetchTx(descriptor, 1, &actual);
  }

  zx_status_t SendRx(uint16_t descriptor) const {
    size_t actual;
    return SendRx(&descriptor, 1, &actual);
  }

  zx_status_t SendTx(uint16_t descriptor) const {
    size_t actual;
    return SendTx(&descriptor, 1, &actual);
  }

  fidl::WireSyncClient<netdev::Session>& session() { return session_; }

  uint64_t canonical_offset(uint16_t index) const { return buffer_length_ * index; }

  const zx::fifo& tx_fifo() const { return fifos_.tx; }
  const zx::fifo& rx_fifo() const { return fifos_.rx; }
  const zx::channel& channel() const { return session_.client_end().channel(); }

 private:
  fidl::Arena<> alloc_;
  uint16_t descriptors_count_{};
  uint64_t buffer_length_{};
  fidl::WireSyncClient<netdev::Session> session_;
  zx::vmo data_vmo_;
  fzl::VmoMapper data_;
  zx::vmo descriptors_vmo_;
  fzl::VmoMapper descriptors_;
  netdev::wire::Fifos fifos_;
};

}  // namespace testing
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_TEST_SESSION_H_
