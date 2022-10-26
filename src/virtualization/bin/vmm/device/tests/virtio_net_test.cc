// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/network/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <fuchsia/net/virtualization/cpp/fidl_test_base.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/trace-provider/provider.h>
#include <zircon/device/ethernet.h>

#include <virtio/net.h>

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"
#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

namespace {

using fuchsia_hardware_network::wire::PortId;
using network::client::NetworkDeviceClient;

constexpr size_t kNumQueues = 2;
constexpr uint16_t kQueueSize = 64;
constexpr size_t kVmoSize = 4096ul * kQueueSize;
constexpr size_t kNetclientNumDescriptors = 16;

constexpr auto kCppComponentUrl = "#meta/virtio_net.cm";
constexpr auto kRustComponentUrl = "#meta/virtio_net_rs.cm";
constexpr auto kComponentName = "virtio_net";
constexpr auto kFakeNetwork = "fake_network";

struct VirtioNetTestParam {
  std::string test_name;
  std::string component_url;
};

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

class FakeNetwork : public fuchsia::net::virtualization::Control,
                    public fuchsia::net::virtualization::Network,
                    public fuchsia::net::virtualization::Interface,
                    public component_testing::LocalComponent {
 public:
  explicit FakeNetwork(async::Loop& loop) : loop_(loop) {}

  // Fake `fuchsia.net.virtualization/Control` implementation.
  void CreateNetwork(
      fuchsia::net::virtualization::Config config,
      fidl::InterfaceRequest<fuchsia::net::virtualization::Network> network) override {
    FX_CHECK(network_binding_set_.bindings().empty())
        << "virtio-net attempted to create multiple networks";
    network_binding_set_.AddBinding(this, std::move(network));
  }

  // Fake `fuchsia.net.virtualization/Network` implementation.
  void AddPort(fidl::InterfaceHandle<fuchsia::hardware::network::Port> port,
               fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) override {
    FX_CHECK(!device_client_.has_value()) << "virtio-net attempted to add multiple devices";
    interface_binding_set_.AddBinding(this, std::move(interface));

    // Get the device backing this port.
    port_ptr_.Bind(std::move(port), loop_.dispatcher());
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
                           loop_.dispatcher());
  }

  void Start(std::unique_ptr<component_testing::LocalComponentHandles> handles) override {
    // This class contains handles to the component's incoming and outgoing capabilities.
    handles_ = std::move(handles);

    ASSERT_EQ(handles_->outgoing()->AddPublicService(
                  control_binding_set_.GetHandler(this, loop_.dispatcher())),
              ZX_OK);
  }

  std::optional<NetworkDeviceClient>& device_client() { return device_client_; }
  std::optional<PortId>& port_id() { return port_id_; }

 private:
  async::Loop& loop_;
  std::unique_ptr<component_testing::LocalComponentHandles> handles_;
  fidl::BindingSet<fuchsia::net::virtualization::Control> control_binding_set_;
  fidl::BindingSet<fuchsia::net::virtualization::Network> network_binding_set_;
  fidl::BindingSet<fuchsia::net::virtualization::Interface> interface_binding_set_;
  fuchsia::hardware::network::PortPtr port_ptr_;
  std::optional<NetworkDeviceClient> device_client_;
  std::optional<PortId> port_id_;
};

class VirtioNetTest : public TestWithDevice,
                      public ::testing::WithParamInterface<VirtioNetTestParam> {
 protected:
  VirtioNetTest()
      : rx_queue_(phys_mem_, kVmoSize * kNumQueues, kQueueSize),
        tx_queue_(phys_mem_, rx_queue_.end(), kQueueSize),
        fake_network_(loop()) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, GetParam().component_url);
    realm_builder.AddLocalChild(kFakeNetwork, &fake_network_);

    realm_builder
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::logger::LogSink::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::net::virtualization::Control::Name_},
                            },
                        .source = {ChildRef{kFakeNetwork}},
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::virtualization::hardware::VirtioNet::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
    net_ = realm_->Connect<fuchsia::virtualization::hardware::VirtioNet>();

    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = MakeStartInfo(tx_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

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
    RunLoopUntil([this] {
      return fake_network_.device_client().has_value() && fake_network_.port_id().has_value();
    });

    // Open a session with the network device.
    {
      std::optional<zx_status_t> result;
      fake_network_.device_client()->OpenSession(
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
      fake_network_.device_client()->AttachPort(
          fake_network_.port_id().value(), {fuchsia_hardware_network::wire::FrameType::kEthernet},
          [&](zx_status_t status) { result = status; });
      RunLoopUntil([&] { return result.has_value(); });
      ASSERT_EQ(*result, ZX_OK);
    }
  }

  fuchsia::virtualization::hardware::VirtioNetPtr net_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  FakeNetwork fake_network_;
  std::unique_ptr<component_testing::RealmRoot> realm_;
};

INSTANTIATE_TEST_SUITE_P(VirtioNetTestInstantiation, VirtioNetTest,
                         ::testing::Values(VirtioNetTestParam{"cpp", kCppComponentUrl},
                                           VirtioNetTestParam{"rust", kRustComponentUrl}),
                         [](const ::testing::TestParamInfo<VirtioNetTestParam>& info) {
                           return info.param.test_name;
                         });

TEST_P(VirtioNetTest, ConnectDisconnect) {
  // Ensure we are connected.
  ASSERT_TRUE(fake_network_.device_client()->HasSession());

  // Kill the session, and wait for it to return.
  ASSERT_EQ(fake_network_.device_client()->KillSession(), ZX_OK);
  bool done = false;
  fake_network_.device_client()->SetErrorCallback([&](zx_status_t status) { done = true; });
  RunLoopUntil([&] { return done; });

  // Ensure the session completed.
  ASSERT_FALSE(fake_network_.device_client()->HasSession());
}

TEST_P(VirtioNetTest, SendToGuest) {
  constexpr uint8_t expected_packet[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

  // Add a descriptor to the RX queue, allowing the guest to receive a packet.
  // Note that the C++ device didn't correctly validate RX buffer lengths.
  // TODO(fxbug.dev/95485): Remove the above note once these tests are improved.
  constexpr size_t kPacketDataSize = 10;
  constexpr size_t kPacketBufferSize = 1526;
  Packet<kPacketBufferSize>* packet;
  zx_status_t status =
      DescriptorChainBuilder(rx_queue_).AppendWritableDescriptor(&packet, sizeof(*packet)).Build();
  net_->NotifyQueue(0);
  ASSERT_EQ(status, ZX_OK);

  // Transmit a packet to the guest.
  ASSERT_NO_FATAL_FAILURE(SendPacketToGuest(
      *fake_network_.device_client(), *fake_network_.port_id(), cpp20::span(expected_packet)));

  // Wait for the device to receive an interrupt.
  status = WaitOnInterrupt();
  ASSERT_EQ(status, ZX_OK);

  // Validate virtio headers.
  EXPECT_EQ(packet->header.num_buffers, 1);
  EXPECT_EQ(packet->header.base.gso_type, VIRTIO_NET_HDR_GSO_NONE);
  EXPECT_EQ(packet->header.base.flags, 0);

  // Validate payload.
  for (size_t i = 0; i != kPacketDataSize; ++i) {
    EXPECT_EQ(packet->data[i], expected_packet[i]);
  }
}

TEST_P(VirtioNetTest, ReceiveFromGuest) {
  std::vector<NetworkDeviceClient::Buffer> received;
  fake_network_.device_client()->SetRxCallback(
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

TEST_P(VirtioNetTest, ResumesReceiveFromGuest) {
  std::mutex mutex;
  std::vector<NetworkDeviceClient::Buffer> received;  // guarded by `mutex`
  fake_network_.device_client()->SetRxCallback([&](NetworkDeviceClient::Buffer buffer) {
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
