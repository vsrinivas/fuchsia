// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_TEST_TEST_TRANSPORT_H_
#define SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_TEST_TEST_TRANSPORT_H_

#include <lib/fastboot/fastboot_base.h>

#include <string>
#include <vector>

namespace fastboot {

using Packets = std::vector<std::string>;

// This class implements a test packet transport to facilitate unit test of
// application code that implements FastbootBase. Test create an instance
// of this test transport and add test input data to it via the AddInPacket()
// interface. The instance can then be passed to FastbootBase::ProcessPacket().
// i.e.
//
//    class Fastboot : public FastbootBase { ... };
//
//    Fastboot fastboot;
//    std::string command = "continue";
//    fastboot::TestTransport transport;
//    transport.AddInPacket(command);
//    zx::result<> ret = fastboot.ProcessPacket(&transport);
class TestTransport : public Transport {
 public:
  // Add a packet to the input stream.
  void AddInPacket(const void* data, size_t size);

  // Add a packet from a container object to the input stream.
  template <typename T>
  void AddInPacket(const T& container) {
    return AddInPacket(container.data(), container.size());
  }

  // Get the packets written to the output.
  const Packets& GetOutPackets() { return out_packets_; }
  void ClearOutPackets() { out_packets_.clear(); }

  // Implementation of the transport interfaces.
  zx::result<size_t> ReceivePacket(void* dst, size_t capacity) override;
  size_t PeekPacketSize() override { return in_packets_.empty() ? 0 : in_packets_.back().size(); }
  zx::result<> Send(std::string_view packet) override;

 private:
  Packets in_packets_;
  Packets out_packets_;
};

}  // namespace fastboot

#endif  // SRC_FIRMWARE_LIB_FASTBOOT_INCLUDE_LIB_FASTBOOT_TEST_TEST_TRANSPORT_H_
