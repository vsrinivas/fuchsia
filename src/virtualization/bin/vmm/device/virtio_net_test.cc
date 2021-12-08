// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/network/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <fuchsia/net/virtualization/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fpromise/bridge.h>
#include <lib/trace-provider/provider.h>
#include <zircon/device/ethernet.h>

#include <virtio/net.h>

#include "lib/fpromise/single_threaded_executor.h"
#include "src/connectivity/lib/network-device/cpp/network_device_client.h"
#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

namespace {

using fuchsia_hardware_network::wire::PortId;
using network::client::NetworkDeviceClient;

constexpr char kVirtioNetUrl[] = "fuchsia-pkg://fuchsia.com/virtio_net#meta/virtio_net.cmx";
constexpr size_t kNumQueues = 2;
constexpr uint16_t kQueueSize = 64;
constexpr size_t kVmoSize = 4096ul * kQueueSize;
constexpr size_t kNetclientNumDescriptors = 16;

// A POD struct representing a virtio-net descriptor.
template <size_t Size>
struct Packet {
  virtio_net_hdr_t header;
  uint8_t data[Size];
} __PACKED;

// Send the given data as an ethernet frame to a NetworkDeviceClient.
void SendPacketToGuest(NetworkDeviceClient& client, PortId port_id,
                       cpp20::span<const uint8_t> payload) {
  // Allocate a buffer.
  NetworkDeviceClient::Buffer buffer = client.AllocTx();
  ASSERT_TRUE(buffer.is_valid()) << "Could not allocate buffer";

  // Set up metadata and copy the data.
  buffer.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
  buffer.data().SetPortId(port_id);
  ASSERT_EQ(buffer.data().Write(payload.data(), payload.size()), payload.size())
      << "Failed to send all data";

  // Send the packet.
  ASSERT_EQ(buffer.Send(), ZX_OK) << "Send failed";
}

class VirtioNetTest : public TestWithDevice,
                      public fuchsia::net::virtualization::Control,
                      public fuchsia::net::virtualization::Network,
                      public fuchsia::net::virtualization::Interface {
 protected:
  VirtioNetTest()
      : rx_queue_(phys_mem_, kVmoSize * kNumQueues, kQueueSize),
        tx_queue_(phys_mem_, rx_queue_.end(), kQueueSize) {}

  void SetUp() override {
    std::unique_ptr<sys::testing::EnvironmentServices> env_services = CreateServices();

    // Add our fake services.
    ASSERT_EQ(ZX_OK, env_services->AddService(control_.GetHandler(this)));

    // Launch device process.
    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status =
        LaunchDevice(kVirtioNetUrl, tx_queue_.end(), &start_info, std::move(env_services));
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(net_.NewRequest());

    fuchsia::hardware::ethernet::MacAddress mac_address = {
        .octets = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff},
    };

    // Start the device, waiting for it to complete before attempting to use it.
    {
      bool done = false;
      net_->Start(std::move(start_info), mac_address, /*enable_bridge=*/true, [&] { done = true; });
      RunLoopUntil([&] { return done; });
    }

    // Configure device virtio queues.
    VirtioQueueFake* queues[kNumQueues] = {&rx_queue_, &tx_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(kVmoSize * i, kVmoSize);
      bool done = false;
      net_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used(), [&] { done = true; });
      RunLoopUntil([&] { return done; });
    }

    // Mark the virtio setup as ready.
    {
      bool done = false;
      net_->Ready(0, [&] { done = true; });
      RunLoopUntil([&] { return done; });
    }

    // Wait for virtio-net to connect to Netstack (i.e., us), add its device, and port
    // information to be fetched.
    RunLoopUntil([this] { return device_client_.has_value() && port_id_.has_value(); });

    // Open a session with the network device.
    {
      std::optional<zx_status_t> result;
      device_client_->OpenSession(
          "virtio_net_test", [&](zx_status_t status) { result = status; },
          [](const network::client::DeviceInfo& dev_info) -> network::client::SessionConfig {
            network::client::SessionConfig config =
                NetworkDeviceClient::DefaultSessionConfig(dev_info);
            // Use the default config, but limit TX/RX descriptors to a small, known
            // number of tests below.
            config.rx_descriptor_count = kNetclientNumDescriptors;
            config.tx_descriptor_count = kNetclientNumDescriptors;
            return config;
          });
      RunLoopUntil([&] { return result.has_value(); });
      ASSERT_EQ(*result, ZX_OK);
    }

    // Attach a port to the session.
    {
      std::optional<zx_status_t> result;
      device_client_->AttachPort(port_id_.value(),
                                 {fuchsia_hardware_network::wire::FrameType::kEthernet},
                                 [&](zx_status_t status) { result = status; });
      RunLoopUntil([&] { return result.has_value(); });
      ASSERT_EQ(*result, ZX_OK);
    }
  }

  // Fake `fuchsia.net.virtualization/Control` implementation.
  void CreateNetwork(
      fuchsia::net::virtualization::Config config,
      fidl::InterfaceRequest<fuchsia::net::virtualization::Network> network) override {
    FX_CHECK(network_.bindings().empty()) << "virtio-net attempted to create multiple networks";
    network_.AddBinding(this, std::move(network));
  }

  // Fake `fuchsia.net.virtualization/Network` implementation.
  void AddPort(fidl::InterfaceHandle<fuchsia::hardware::network::Port> port,
               fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) override {
    FX_CHECK(!device_client_.has_value()) << "virtio-net attempted to add multiple devices";
    interface_.AddBinding(this, std::move(interface));

    // Get the device backing this port.
    port_ptr_.Bind(std::move(port), dispatcher());
    fidl::InterfaceHandle<fuchsia::hardware::network::Device> device;
    port_ptr_->GetDevice(device.NewRequest());

    // Get the PortId of this port.
    port_ptr_->GetInfo([this](fuchsia::hardware::network::PortInfo info) {
      port_id_ = PortId{
          .base = info.id().base,
          .salt = info.id().salt,
      };
    });

    // Create a NetworkDeviceClient to the device.
    device_client_.emplace(fidl::ClientEnd<fuchsia_hardware_network::Device>(device.TakeChannel()),
                           dispatcher());
  }

  fuchsia::virtualization::hardware::VirtioNetPtr net_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  fidl::BindingSet<fuchsia::net::virtualization::Control> control_;
  fidl::BindingSet<fuchsia::net::virtualization::Network> network_;
  fidl::BindingSet<fuchsia::net::virtualization::Interface> interface_;
  fuchsia::hardware::network::PortPtr port_ptr_;
  std::optional<NetworkDeviceClient> device_client_;
  std::optional<PortId> port_id_;
};

TEST_F(VirtioNetTest, ConnectDisconnect) {
  // Ensure we are connected.
  ASSERT_TRUE(device_client_->HasSession());

  // Kill the session, and wait for it to return.
  ASSERT_EQ(device_client_->KillSession(), ZX_OK);
  bool done = false;
  device_client_->SetErrorCallback([&](zx_status_t status) { done = true; });
  RunLoopUntil([&] { return done; });

  // Ensure the session completed.
  ASSERT_FALSE(device_client_->HasSession());
}

TEST_F(VirtioNetTest, SendToGuest) {
  constexpr uint8_t expected_packet[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

  // Add a descriptor to the RX queue, allowing the guest to receive a packet.
  constexpr size_t kPacketSize = 10;
  Packet<kPacketSize>* packet;
  zx_status_t status =
      DescriptorChainBuilder(rx_queue_).AppendWritableDescriptor(&packet, sizeof(*packet)).Build();
  ASSERT_EQ(status, ZX_OK);

  // Transmit a packet to the guest.
  ASSERT_NO_FATAL_FAILURE(
      SendPacketToGuest(*device_client_, *port_id_, cpp20::span(expected_packet)));

  // Wait for the device to receive an interrupt.
  status = WaitOnInterrupt();
  ASSERT_EQ(status, ZX_OK);

  // Validate virtio headers.
  EXPECT_EQ(packet->header.num_buffers, 1);
  EXPECT_EQ(packet->header.gso_type, VIRTIO_NET_HDR_GSO_NONE);
  EXPECT_EQ(packet->header.flags, 0);

  // Validate payload.
  for (size_t i = 0; i != kPacketSize; ++i) {
    EXPECT_EQ(packet->data[i], expected_packet[i]);
  }
}

TEST_F(VirtioNetTest, ReceiveFromGuest) {
  std::vector<NetworkDeviceClient::Buffer> received;
  device_client_->SetRxCallback(
      [&](NetworkDeviceClient::Buffer buffer) { received.push_back(std::move(buffer)); });

  // Add packet to virtio TX queue.
  constexpr size_t kPacketSize = 10;
  Packet<kPacketSize> packet = {
      .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
  };
  zx_status_t status =
      DescriptorChainBuilder(tx_queue_).AppendReadableDescriptor(&packet, sizeof(packet)).Build();
  ASSERT_EQ(status, ZX_OK);
  net_->NotifyQueue(1);

  // Ensure the packet was received.
  RunLoopUntil([&] { return !received.empty(); });
  ASSERT_EQ(received.size(), 1u);
  EXPECT_TRUE(received[0].is_valid());

  // Ensure data is correct.
  EXPECT_EQ(received[0].data().frame_type(), fuchsia_hardware_network::wire::FrameType::kEthernet);
  EXPECT_EQ(received[0].data().len(), kPacketSize);
  uint8_t received_data[kPacketSize];
  received[0].data().Read(received_data, kPacketSize);
  EXPECT_EQ(std::basic_string_view(received_data, kPacketSize),
            std::basic_string_view(packet.data, kPacketSize));
}

TEST_F(VirtioNetTest, ResumesReceiveFromGuest) {
  std::mutex mutex;
  std::vector<NetworkDeviceClient::Buffer> received;  // guarded by `mutex`
  device_client_->SetRxCallback([&](NetworkDeviceClient::Buffer buffer) {
    std::lock_guard guard(mutex);
    received.push_back(std::move(buffer));
  });

  // Build more descriptors than can be simultaneously processed by the NetworkDeviceClient.
  constexpr size_t kPacketsToSend = 2 * kNetclientNumDescriptors;
  for (size_t i = 0; i < kPacketsToSend; i++) {
    constexpr size_t kPacketSize = 10;
    Packet<kPacketSize> data = {};
    zx_status_t status =
        DescriptorChainBuilder(tx_queue_).AppendReadableDescriptor(&data, sizeof(data)).Build();
    ASSERT_EQ(status, ZX_OK);
  }

  // Notify the device about the descriptors we built.
  net_->NotifyQueue(1);

  // We are not handing packets back to the NetworkClient, so we expect that
  // after being sent kNetclientNumDescriptors, the client will refuse to
  // process any more.
  RunLoopUntil([&] {
    std::lock_guard guard(mutex);
    return received.size() >= kNetclientNumDescriptors;
  });
  {
    std::lock_guard guard(mutex);
    EXPECT_EQ(received.size(), kNetclientNumDescriptors);

    // Return the buffers back to the network client.
    received.clear();
  }

  // The device should continue to process the rest of the descriptors without
  // being notified by the guest (i.e., without a call to NotifyQueue).
  RunLoopUntil([&] {
    std::lock_guard guard(mutex);
    return received.size() == kNetclientNumDescriptors;
  });
  std::lock_guard guard(mutex);
  EXPECT_EQ(received.size(), kNetclientNumDescriptors);
}

}  // namespace
