// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_netstack.h"

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/defer.h>
#include <lib/fit/thread_checker.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scope.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>

#include <future>
#include <queue>

#include <gtest/gtest.h>

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"

namespace {

using network::client::NetworkDeviceClient;

using Packet = std::vector<uint8_t>;

constexpr uint32_t kMtu = 1500;

constexpr uint8_t kHostMacAddress[ETH_ALEN] = {0x02, 0x1a, 0x11, 0x00, 0x00, 0x00};

constexpr uint8_t kHostIpv4Address[4] = {192, 168, 0, 1};
constexpr uint8_t kGuestIpv4Address[4] = {192, 168, 0, 10};

constexpr uint16_t kProtocolIpv4 = 0x0800;
constexpr uint8_t kPacketTypeUdp = 17;
constexpr uint16_t kTestPort = 4242;

// Compare MacAddress using lexicographical ordering.
struct MacAddressComparator {
  bool operator()(const fuchsia::hardware::ethernet::MacAddress& a,
                  const fuchsia::hardware::ethernet::MacAddress& b) const {
    return std::lexicographical_compare(a.octets.begin(), a.octets.end(), b.octets.begin(),
                                        b.octets.end());
  }
};

// Calculate the IPv4 checksum of the given packet.
uint16_t Checksum(const void* _data, size_t len, uint16_t _sum) {
  uint32_t sum = _sum;
  auto data = static_cast<const uint16_t*>(_data);
  for (; len > 1; len -= 2) {
    sum += *data++;
  }
  if (len) {
    sum += (*data & UINT8_MAX);
  }
  while (sum > UINT16_MAX) {
    sum = (sum & UINT16_MAX) + (sum >> 16);
  }
  return static_cast<uint16_t>(~sum);
}

// Copy the data out of the given NetworkDeviceClient::Buffer.
Packet CopyPacketFromBuffer(NetworkDeviceClient::Buffer& buffer) {
  Packet result(buffer.data().len());
  size_t copied = buffer.data().Read(result.data(), result.size());
  FX_CHECK(copied == result.size()) << "Expected " << result.size() << " byte(s) to be copied, but "
                                    << copied << " byte(s) copied.";
  return result;
}

// Run the given functions on the given dispatcher, blocking until the lambda
// has completed.
//
// Will deadlock if the current thread is already running on `dispatcher`, and
// no other threads are available.
void RunOnExecutorSync(async::Executor& executor, fit::function<void()> workload) {
  fpromise::run_single_threaded(
      fpromise::schedule_for_consumer(&executor, fpromise::make_promise(std::move(workload)))
          .promise());
}

}  // namespace

namespace fake_netstack::internal {

// A network device connected to the fake network.
//
// Thread hostile: construction and methods should all be called on the thread
// backing the single-threaded `executor`.
class Device : public fuchsia::net::virtualization::Interface {
 public:
  // Create and configure a new device.
  //
  // The returned promise is resolved once the device has been configured and
  // is ready to send and receive packets.
  static fpromise::promise<std::unique_ptr<Device>, zx_status_t> Create(
      async::Executor* executor, fidl::InterfaceHandle<fuchsia::hardware::network::Port> port,
      fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) {
    // State persisting across future executions.
    struct State {
      fuchsia::hardware::network::PortPtr port_ptr;
      fidl::InterfaceHandle<fuchsia::hardware::network::Device> device_handle;
      fuchsia_hardware_network::wire::PortId port_id;
      std::unique_ptr<Device> device;
    };
    auto state = std::make_unique<State>();

    // Establish a connection to the Device backing the the Port.
    state->port_ptr = port.Bind();
    state->port_ptr->GetDevice(state->device_handle.NewRequest());

    // Fetch the port's ID.
    fpromise::bridge<void, zx_status_t> bridge;
    auto completer =
        std::make_shared<fpromise::completer<void, zx_status_t>>(std::move(bridge.completer));
    state->port_ptr.set_error_handler(
        [completer](zx_status_t status) { completer->complete_error(status); });
    state->port_ptr->GetInfo([completer = std::move(completer),
                              state = state.get()](fuchsia::hardware::network::PortInfo info) {
      state->port_id = {
          .base = info.id().base,
          .salt = info.id().salt,
      };
      completer->complete_ok();
    });

    // Create the device object.
    auto create_device = [state = state.get(), interface = std::move(interface),
                          executor]() mutable {
      // Create the Device object.
      //
      // Using `new` to allow access to private constructor.
      state->device = std::unique_ptr<Device>(new Device(
          executor->dispatcher(), std::move(state->device_handle), std::move(interface)));
      return fpromise::ok();
    };

    // Get the client's MAC address.
    auto fetch_device_mac_address = [state = state.get()]() {
      fpromise::bridge<void, zx_status_t> bridge;
      std::lock_guard guard(state->device->checker_);
      state->device->client_.GetPortInfoWithMac(
          state->port_id, [state, completer = std::move(bridge.completer)](
                              zx::result<network::client::PortInfoAndMac> result) mutable {
            if (result.is_error()) {
              FX_PLOGS(WARNING, result.status_value()) << "Could not fetch device port information";
              completer.complete_error(result.status_value());
              return;
            }
            state->device->port_info_ = std::move(result.value());
            completer.complete_ok();
          });
      return bridge.consumer.promise();
    };

    // Open a session so the device is ready to use.
    auto open_session = [state = state.get()]() mutable -> fpromise::promise<void, zx_status_t> {
      std::lock_guard guard(state->device->checker_);
      fpromise::bridge<void, zx_status_t> bridge;
      state->device->client_.OpenSession(
          "fake-netstack-session",
          [completer = std::move(bridge.completer)](zx_status_t status) mutable {
            if (status != ZX_OK) {
              FX_PLOGS(ERROR, status) << "Error opening device session";
              completer.complete_error(status);
              return;
            }
            completer.complete_ok();
          });
      return bridge.consumer.promise();
    };

    // Open the requested port.
    auto attach_port = [state = state.get()]() -> fpromise::promise<void, zx_status_t> {
      std::lock_guard guard(state->device->checker_);
      fpromise::bridge<void, zx_status_t> bridge;
      state->device->client_.AttachPort(
          state->port_id, {fuchsia_hardware_network::wire::FrameType::kEthernet},
          [completer = std::move(bridge.completer)](zx_status_t status) mutable {
            if (status != ZX_OK) {
              FX_PLOGS(ERROR, status) << "Error attaching to device port";
              completer.complete_error(status);
              return;
            }
            completer.complete_ok();
          });
      return bridge.consumer.promise();
    };

    // Return the completed device to caller.
    auto return_device =
        [state = state.get()]() -> fpromise::result<std::unique_ptr<Device>, zx_status_t> {
      return fpromise::ok(std::move(state->device));
    };

    return bridge.consumer.promise()
        .and_then(std::move(create_device))
        .and_then(std::move(fetch_device_mac_address))
        .and_then(std::move(open_session))
        .and_then(std::move(attach_port))
        .and_then(std::move(return_device))
        // Keep `state` alive until future is resolved.
        .inspect([state = std::move(state)](
                     const fpromise::result<std::unique_ptr<Device>, zx_status_t>&) {});
  }

  const network::client::PortInfoAndMac& port_info() { return port_info_; }

  // Read the first available packet received by the device.
  fpromise::promise<Packet, zx_status_t> ReadPacket() {
    std::lock_guard guard(checker_);

    // If there is already a packet waiting, just return it directly.
    if (!packets_.empty()) {
      Packet result = std::move(packets_.front());
      packets_.pop();
      return fpromise::make_result_promise<Packet, zx_status_t>(fpromise::ok(std::move(result)));
    }

    // Otherwise, wait the next packet arrives.
    fpromise::bridge<Packet, zx_status_t> bridge;
    waiters_.push(std::move(bridge.completer));
    return bridge.consumer.promise();
  }

  // Transmit a packet over the device.
  fpromise::promise<void, zx_status_t> WritePacket(Packet payload) {
    std::lock_guard guard(checker_);

    // Allocate a buffer.
    NetworkDeviceClient::Buffer buffer = client_.AllocTx();
    if (!buffer.is_valid()) {
      return fpromise::make_result_promise<void, zx_status_t>(fpromise::error(ZX_ERR_NO_RESOURCES));
    }

    // Set up metadata and copy the data.
    buffer.data().SetFrameType(fuchsia_hardware_network::wire::FrameType::kEthernet);
    buffer.data().SetPortId(port_info_.id);
    size_t transmitted = buffer.data().Write(payload.data(), payload.size());
    FX_CHECK(transmitted == payload.size())
        << "Expected " << payload.size() << " byte(s) to be transmitted, but only " << transmitted
        << " byte(s) were.";

    // Send the packet.
    zx_status_t status = buffer.Send();
    if (status != ZX_OK) {
      return fpromise::make_result_promise<void, zx_status_t>(fpromise::error(status));
    }

    return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
  }

 private:
  Device(async_dispatcher_t* dispatcher,
         fidl::InterfaceHandle<fuchsia::hardware::network::Device> device_server,
         fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface)
      : client_(fidl::ClientEnd<fuchsia_hardware_network::Device>(device_server.TakeChannel()),
                dispatcher),
        interface_(this, std::move(interface), dispatcher) {
    // Register for error notifications.
    client_.SetErrorCallback([this](zx_status_t status) {
      std::lock_guard guard(checker_);
      FX_LOGS(WARNING) << "Ethernet client registered error: " << zx_status_get_string(status);
      interface_.Close(status);
    });
    // Register for packet arrivals.
    client_.SetRxCallback(
        [this](NetworkDeviceClient::Buffer buffer) { PacketReceived(std::move(buffer)); });
  }

  // Called when a packet has been received by the device.
  void PacketReceived(NetworkDeviceClient::Buffer buffer) {
    std::lock_guard guard(checker_);

    // If nobody is waiting for a packet, simply add the packet to the queue.
    if (waiters_.empty()) {
      packets_.push(CopyPacketFromBuffer(buffer));
      return;
    }

    // Otherwise, give the packet to a waiter.
    waiters_.front().complete_ok(CopyPacketFromBuffer(buffer));
    waiters_.pop();
  }

  fit::thread_checker checker_;
  NetworkDeviceClient client_ __TA_GUARDED(checker_);
  fidl::Binding<fuchsia::net::virtualization::Interface> interface_ __TA_GUARDED(checker_);
  network::client::PortInfoAndMac port_info_ = {};

  std::queue<Packet> packets_ __TA_GUARDED(checker_);  // Received packets
  std::queue<fpromise::completer<Packet, zx_status_t>> waiters_
      __TA_GUARDED(checker_);  // Waiters for packets
};

// A fake network, consisting of one or more devices.
//
// This class implements both the `fuchsia.net.virtualization/Control` and
// `fuchsia.net.virtualization/Network` FIDL protocols.
//
// The Control API is able to create multiple independent networks, but this
// class simply maps them all into a single network for simplicity.
//
// Thread hostile: unless specified otherwise, construction,
// destruction, and methods should all be called on the thread backing
// the single-threaded `executor`.
class FakeNetwork : public fuchsia::net::virtualization::Control,
                    public fuchsia::net::virtualization::Network {
 public:
  explicit FakeNetwork(async::Executor* executor) : executor_(executor) {}
  ~FakeNetwork() override { FX_DCHECK(checker_.is_thread_valid()); }

  // Get an interface request handler.
  //
  // This method and the returned handler are thread safe.
  fidl::InterfaceRequestHandler<fuchsia::net::virtualization::Control> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::net::virtualization::Control> request) {
      async::PostTask(executor_->dispatcher(), [request = std::move(request), this]() mutable {
        std::lock_guard guard(checker_);
        control_bindings_.AddBinding(this, std::move(request), executor_->dispatcher());
      });
    };
  }

  // Wait for a device with the given MAC address to be added to this network,
  // and then return it.
  fpromise::promise<Device*, zx_status_t> GetDevice(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr) {
    std::lock_guard guard(checker_);

    // If the device is already connected the the netstack then just return
    // a pointer to it.
    auto it = devices_.find(mac_addr);
    if (it != devices_.end()) {
      return fpromise::make_result_promise<Device*, zx_status_t>(fpromise::ok(it->second.get()));
    }

    // Otherwise, add to the list of completers for this MAC address. The
    // promise will complete when the devices calls AddEthernetDevice.
    fpromise::bridge<Device*, zx_status_t> bridge;
    auto completers_it = completers_.find(mac_addr);
    if (completers_it == completers_.end()) {
      std::vector<fpromise::completer<Device*, zx_status_t>> vec;
      vec.push_back(std::move(bridge.completer));
      completers_.insert(std::make_pair(mac_addr, std::move(vec)));
    } else {
      completers_it->second.push_back(std::move(bridge.completer));
    }

    return bridge.consumer.promise();
  }

  // `fuchsia.net.virtualization/Control` implementation.
  void CreateNetwork(
      fuchsia::net::virtualization::Config config,
      fidl::InterfaceRequest<fuchsia::net::virtualization::Network> network) override {
    std::lock_guard guard(checker_);

    // We only support bridged connections.
    if (!config.is_bridged()) {
      FX_LOGS(ERROR) << "FakeNetstack only supports bridged connections. Received: "
                     << config.Ordinal();
      network.Close(ZX_ERR_NOT_SUPPORTED);
      return;
    }

    // Create a new network.
    network_bindings_.AddBinding(this, std::move(network), executor_->dispatcher());
  }

  // `fuchsia.net.virtualization/Network` implementation.
  void AddPort(fidl::InterfaceHandle<fuchsia::hardware::network::Port> port,
               fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) override {
    std::lock_guard guard(checker_);

    // Create the device and track it.
    fpromise::schedule_for_consumer(executor_,
                                    Device::Create(executor_, std::move(port), std::move(interface))
                                        .and_then([this](std::unique_ptr<Device>& device) {
                                          AddReadyDevice(std::move(device));
                                        })
                                        .wrap_with(scope_));
  }

 private:
  // Add the given device to this network.
  void AddReadyDevice(std::unique_ptr<Device> device) {
    std::lock_guard guard(checker_);

    // Get the device's MAC address, aborting if one does not exist.
    if (!device->port_info().unicast_address.has_value()) {
      FX_LOGS(WARNING) << "Ignoring attempt to add device without a MAC address";
      return;
    }
    fuchsia::hardware::ethernet::MacAddress device_mac;
    memcpy(&device_mac.octets[0], device->port_info().unicast_address->octets.data(),
           sizeof(device_mac.octets));

    // Add the device.
    auto [it, success] = devices_.insert(std::make_pair(device_mac, std::move(device)));
    if (!success) {
      FX_LOGS(WARNING) << "Ignoring attempt to add device with duplicate MAC address";
      return;
    }

    // Resolve any pending promises to fetch this device.
    auto completers_it = completers_.find(device_mac);
    if (completers_it == completers_.end()) {
      return;
    }
    while (!completers_it->second.empty()) {
      completers_it->second.back().complete_ok(it->second.get());
      completers_it->second.pop_back();
    }
  }

  fit::thread_checker checker_;

  async::Executor* executor_;  // Owned elsewhere.

  fidl::BindingSet<fuchsia::net::virtualization::Control> control_bindings_ __TA_GUARDED(checker_);
  fidl::BindingSet<fuchsia::net::virtualization::Network> network_bindings_ __TA_GUARDED(checker_);

  // Maps MAC addresses to devices.
  std::map<fuchsia::hardware::ethernet::MacAddress, std::unique_ptr<Device>, MacAddressComparator>
      devices_ __TA_GUARDED(checker_);

  // Maps MAC addresses to completers, to enable the GetDevice promises.
  std::map<fuchsia::hardware::ethernet::MacAddress,
           std::vector<fpromise::completer<Device*, zx_status_t>>, MacAddressComparator>
      completers_ __TA_GUARDED(checker_);

  fpromise::scope scope_ __TA_GUARDED(checker_);
};

}  // namespace fake_netstack::internal

FakeNetstack::FakeNetstack()
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread), executor_(loop_.dispatcher()) {
  // Start a thread for the Device waiters.
  //
  // We can't use the main test thread, because it will block to run test
  // utils and deadlock the test.
  loop_.StartThread("fake-netstack-v2-thread");

  // Construct the FakeNetwork on the dispatcher thread.
  //
  // FakeNetwork has thread-hostile components, so we ensure that
  // construction/destruction/method calls all occur on the executor's thread.
  RunOnExecutorSync(executor_, [this] {
    network_ = std::make_unique<fake_netstack::internal::FakeNetwork>(&executor_);
  });
}

FakeNetstack::~FakeNetstack() {
  // Destruct the thread-hostile FakeNetwork instance on the dispatcher thread.
  RunOnExecutorSync(executor_, [network = std::move(network_)]() mutable { network.reset(); });
  // Even once RunOnExecutorSync has completed the executor_ could still be running a task. This is
  // because for us to know our 'task' completed, it gets wrapped in another task that signals us of
  // completion. But now there is a chance of a race where we unblock and then finish this
  // destructor before the executor_ finishes running the rest of the task and gets back to idle.
  // Therefore we need to perform a graceful shutdown of the async loops thread to ensure there is
  // definitely nothing running before shutting down the executor.
  loop_.Quit();
  loop_.JoinThreads();
}

fpromise::promise<void, zx_status_t> FakeNetstack::SendUdpPacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet) {
  size_t total_length = sizeof(ethhdr) + sizeof(iphdr) + sizeof(udphdr) + packet.size();
  if (total_length > kMtu) {
    return fpromise::make_error_promise(ZX_ERR_BUFFER_TOO_SMALL);
  }

  std::vector<uint8_t> udp_packet;
  udp_packet.reserve(total_length);

  {
    ethhdr eth;
    static_assert(sizeof(eth.h_dest) == sizeof(mac_addr.octets));
    memcpy(eth.h_dest, mac_addr.octets.data(), sizeof(eth.h_dest));
    static_assert(sizeof(eth.h_source) == sizeof(kHostMacAddress));
    memcpy(eth.h_source, kHostMacAddress, sizeof(eth.h_source));
    eth.h_proto = htons(kProtocolIpv4);
    std::copy_n(reinterpret_cast<uint8_t*>(&eth), sizeof(eth), std::back_inserter(udp_packet));
  }

  {
    iphdr ip = {
        .version = 4,
        .tot_len = htons(static_cast<uint16_t>(sizeof(iphdr) + sizeof(udphdr) + packet.size())),
        .ttl = UINT8_MAX,
        .protocol = kPacketTypeUdp,
    };
    ip.ihl = sizeof(iphdr) >> 2;  // Header length in 32-bit words.
    static_assert(sizeof(ip.saddr) == sizeof(kHostIpv4Address));
    memcpy(&ip.saddr, kHostIpv4Address, sizeof(ip.saddr));
    static_assert(sizeof(ip.daddr) == sizeof(kGuestIpv4Address));
    memcpy(&ip.daddr, kGuestIpv4Address, sizeof(ip.daddr));
    ip.check = Checksum(&ip, sizeof(iphdr), 0);
    std::copy_n(reinterpret_cast<uint8_t*>(&ip), sizeof(ip), std::back_inserter(udp_packet));
  }

  {
    udphdr udp = {
        .source = htons(kTestPort),
        .dest = htons(kTestPort),
        .len = htons(static_cast<uint16_t>(sizeof(udphdr) + packet.size())),
    };
    std::copy_n(reinterpret_cast<uint8_t*>(&udp), sizeof(udp), std::back_inserter(udp_packet));
  }

  std::copy(packet.begin(), packet.end(), std::back_inserter(udp_packet));

  return SendPacket(mac_addr, std::move(udp_packet));
}

fpromise::promise<void, zx_status_t> FakeNetstack::SendPacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet) {
  if (packet.size() > kMtu) {
    return fpromise::make_error_promise(ZX_ERR_INVALID_ARGS);
  }

  return fpromise::schedule_for_consumer(
             &executor_,
             fpromise::make_promise([mac_addr, this]() { return network_->GetDevice(mac_addr); })
                 .and_then(
                     [packet = std::move(packet)](fake_netstack::internal::Device* const& device) {
                       return device->WritePacket(packet);
                     }))
      .promise();
}

fpromise::promise<std::vector<uint8_t>, zx_status_t> FakeNetstack::ReceivePacket(
    const fuchsia::hardware::ethernet::MacAddress& mac_addr) {
  return fpromise::schedule_for_consumer(
             &executor_,
             fpromise::make_promise([mac_addr, this]() { return network_->GetDevice(mac_addr); })
                 .and_then([](fake_netstack::internal::Device* const& device) {
                   return device->ReadPacket();
                 })
                 .and_then([](Packet& packet) -> fpromise::result<Packet, zx_status_t> {
                   return fpromise::ok(std::move(packet));
                 }))
      .promise();
}

void FakeNetstack::Start(std::unique_ptr<component_testing::LocalComponentHandles> handles) {
  // This class contains handles to the component's incoming and outgoing capabilities.
  handles_ = std::move(handles);

  ASSERT_EQ(handles_->outgoing()->AddPublicService(network_->GetHandler()), ZX_OK);
}
