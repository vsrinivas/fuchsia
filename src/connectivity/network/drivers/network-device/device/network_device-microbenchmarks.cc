// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <perftest/perftest.h>

#include "device_interface.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "test_session.h"

#define ZX_ASSERT_OK(status, msg) \
  ZX_ASSERT_MSG((status) == ZX_OK, msg " %s", zx_status_get_string(status))

namespace network {

class FakeDeviceImpl : public ddk::NetworkPortProtocol<FakeDeviceImpl>,
                       public ddk::NetworkDeviceImplProtocol<FakeDeviceImpl> {
 public:
  static constexpr uint16_t kDepth = 256;
  static constexpr uint16_t kPortId = 1;
  static constexpr uint32_t kMtu = 1500;
  static constexpr uint8_t kRxFrameTypes[] = {
      static_cast<uint8_t>(netdev::wire::FrameType::kEthernet),
  };
  static constexpr tx_support_t kTxFrameTypes[] = {
      {.type = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet)},
  };

  FakeDeviceImpl(perftest::RepeatState* state) : perftest_state_(state) {}

  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface) {
    iface_ = ddk::NetworkDeviceIfcProtocolClient(iface);
    iface_.AddPort(kPortId, this, &network_port_protocol_ops_);
    return ZX_OK;
  }
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie) {
    callback(cookie, ZX_OK);
  }
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie) {
    callback(cookie);
  }
  void NetworkDeviceImplGetInfo(device_info_t* out_info) {
    *out_info = {
        .tx_depth = kDepth,
        .rx_depth = kDepth,
        .rx_threshold = kDepth,
        .max_buffer_length = ZX_PAGE_SIZE / 2,
        .buffer_alignment = ZX_PAGE_SIZE,
    };
  }

  void NetworkDeviceImplQueueTx(const tx_buffer_t* buf_list, size_t buf_count) {
    ZX_ASSERT_MSG(buf_count <= kDepth, "received %ld tx buffers (depth = %d)", buf_count, kDepth);
    // NB: This may be called on a thread different than the test thread. To guarantee this doesn't
    // happen concurrently with other perftest actions, the latency test must make sure that no
    // descriptors belong to the device upon each test iteration.
    perftest_state_->NextStep();
    std::array<tx_result_t, kDepth> result;
    auto iter = result.begin();
    for (auto& buff : cpp20::span(buf_list, buf_count)) {
      *iter++ = {
          .id = buff.id,
          .status = ZX_OK,
      };
    }
    iface_.CompleteTx(result.begin(), buf_count);
  }

  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buf_list, size_t buf_count) {
    ZX_ASSERT_MSG(buf_count <= kDepth, "received %ld tx buffers (depth = %d)", buf_count, kDepth);
    // NB: This may be called on a thread different than the test thread. To guarantee this doesn't
    // happen concurrently with other perftest actions, the latency test must make sure that no
    // descriptors belong to the device upon each test iteration.
    perftest_state_->NextStep();
    std::array<rx_buffer_t, kDepth> result;
    std::array<rx_buffer_part_t, kDepth> parts;
    auto result_iter = result.begin();
    auto part_iter = parts.begin();
    for (auto& buff : cpp20::span(buf_list, buf_count)) {
      auto& part = *part_iter++;
      part = {
          .id = buff.id,
          // Any length different than zero will cause the buffer to reach the session, it's
          // irrelevant for the performance test.
          .length = 1024,
      };
      *result_iter++ = {
          .meta =
              {
                  .port = kPortId,
                  .frame_type = static_cast<uint8_t>(netdev::wire::FrameType::kEthernet),
              },
          .data_list = &part,
          .data_count = 1};
    }
    iface_.CompleteRx(result.begin(), buf_count);
  }

  ddk::NetworkDeviceImplProtocolClient client() {
    network_device_impl_protocol_t proto = {
        .ops = &network_device_impl_protocol_ops_,
        .ctx = this,
    };
    return ddk::NetworkDeviceImplProtocolClient(&proto);
  }

  void NetworkDeviceImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback,
                                   void* cookie) {
    callback(cookie, ZX_OK);
  }
  void NetworkDeviceImplReleaseVmo(uint8_t vmo_id) {}
  void NetworkDeviceImplSetSnoop(bool snoop) { ZX_PANIC("unexpected call to SetSnoop(%d)", snoop); }
  void NetworkPortGetInfo(port_info_t* out_info) {
    *out_info = {
        .port_class = static_cast<uint8_t>(netdev::wire::DeviceClass::kEthernet),
        .rx_types_list = kRxFrameTypes,
        .rx_types_count = std::size(kRxFrameTypes),
        .tx_types_list = kTxFrameTypes,
        .tx_types_count = std::size(kTxFrameTypes),
    };
  }
  void NetworkPortGetStatus(port_status_t* out_status) {
    *out_status = {
        .mtu = kMtu,
        .flags = static_cast<uint32_t>(netdev::wire::StatusFlags::kOnline),
    };
  }
  void NetworkPortSetActive(bool active) {}
  void NetworkPortGetMac(mac_addr_protocol_t* out_mac_ifc) { *out_mac_ifc = {}; }
  void NetworkPortRemoved() {}

 private:
  ddk::NetworkDeviceIfcProtocolClient iface_;
  perftest::RepeatState* const perftest_state_;
};

}  // namespace network

// NB: BaseTestSession is laid out to make the contract clear, we avoid declaring variables with it
// to avoid dynamic dispatch.
class BaseTestSession : public network::testing::TestSession {
 public:
  virtual ~BaseTestSession() = default;
  virtual zx_status_t SendDescriptors(const uint16_t* descriptors, size_t count,
                                      size_t* actual) = 0;
  virtual zx_status_t FetchDescriptors(uint16_t* descriptors, size_t count, size_t* actual) = 0;
  virtual const zx::fifo& test_fifo() = 0;
};

class TxTestSession : public BaseTestSession {
 public:
  zx_status_t SendDescriptors(const uint16_t* descriptors, size_t count, size_t* actual) override {
    return SendTx(descriptors, count, actual);
  }
  zx_status_t FetchDescriptors(uint16_t* descriptors, size_t count, size_t* actual) override {
    return FetchTx(descriptors, count, actual);
  }
  const zx::fifo& test_fifo() override { return tx_fifo(); }
};

class RxTestSession : public BaseTestSession {
 public:
  zx_status_t SendDescriptors(const uint16_t* descriptors, size_t count, size_t* actual) override {
    return SendRx(descriptors, count, actual);
  }
  zx_status_t FetchDescriptors(uint16_t* descriptors, size_t count, size_t* actual) override {
    return FetchRx(descriptors, count, actual);
  }
  const zx::fifo& test_fifo() override { return rx_fifo(); }
};

// LatencyTest measures the round trip latency between a client and a device using an in-process
// fake network device.
//
// The total round trip latency is the time taken for the client to send a batch of packet buffers
// to the device and get them back. This breaks down as follows:
//   - The outbound latency is the time between writing to the FIFO and observing the buffers
//   reaching the device.
//   - The return latency is the time it takes from the device fulfilling those buffers and the
//   client observing them be returned on the FIFO.
//
// The template parameter determines which FIFO and, hence, which path (rx/tx), we're measuring the
// total latency on. Another variation on the test is the number of buffers offered and returned in
// a single batch (limited to the device's FIFO depth).
template <class Session>
bool LatencyTest(perftest::RepeatState* state, const uint16_t buffer_count) {
  ZX_ASSERT_MSG(buffer_count <= network::FakeDeviceImpl::kDepth,
                "can't measure latency with more buffers (%d) than device depth (%d)", buffer_count,
                network::FakeDeviceImpl::kDepth);
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  zx_status_t status = loop.StartThread("netdevice-dispatcher");
  ZX_ASSERT_OK(status, "failed to start thread");

  network::FakeDeviceImpl impl(state);

  zx::result device_status =
      network::internal::DeviceInterface::Create(loop.dispatcher(), impl.client());
  ZX_ASSERT_OK(device_status.status_value(), "failed to create device");
  std::unique_ptr device = std::move(device_status.value());

  zx::result device_endpoints = fidl::CreateEndpoints<network::netdev::Device>();
  ZX_ASSERT_OK(device_endpoints.status_value(), "failed to create device endpoints");
  ZX_ASSERT_OK(device->Bind(std::move(device_endpoints->server)), "failed to bind to device");

  zx::result port_endpoints = fidl::CreateEndpoints<network::netdev::Port>();
  ZX_ASSERT_OK(port_endpoints.status_value(), "failed to create port endpoints");
  ZX_ASSERT_OK(
      device->BindPort(network::FakeDeviceImpl::kPortId, std::move(port_endpoints->server)),
      "failed to bind port");
  fidl::WireSyncClient port{(std::move(port_endpoints->client))};
  fidl::WireResult port_info_result = port->GetInfo();
  ZX_ASSERT_OK(port_info_result.status(), "failed to get port info");
  const network::netdev::wire::PortInfo& port_info = port_info_result->info;
  ZX_ASSERT_MSG(port_info.has_id(), "port id missing");
  const network::netdev::wire::PortId& port_id = port_info.id();

  Session session;
  fidl::WireSyncClient client{std::move(device_endpoints->client)};
  status =
      session.Open(client, "session", network::netdev::wire::SessionFlags::kPrimary, buffer_count);
  ZX_ASSERT_OK(status, "failed to open session");
  status = session.AttachPort(port_id, {network::netdev::wire::FrameType::kEthernet});
  ZX_ASSERT_OK(status, "failed to attach port");

  std::array<uint16_t, network::FakeDeviceImpl::kDepth> write_descriptors, returned_descriptors;
  for (uint16_t i = 0; i < buffer_count; i++) {
    buffer_descriptor_t& descriptor = session.ResetDescriptor(i);
    // Tx tests need to set the port id here.
    descriptor.port_id = {
        .base = port_id.base,
        .salt = port_id.salt,
    };
    write_descriptors[i] = i;
  }

  state->DeclareStep("outbound");
  state->DeclareStep("return");
  while (state->KeepRunning()) {
    size_t actual;
    status = session.SendDescriptors(write_descriptors.begin(), buffer_count, &actual);
    ZX_ASSERT_OK(status, "failed to send descriptors");
    ZX_ASSERT_MSG(actual == buffer_count, "partial FIFO write %ld/%d", actual, buffer_count);

    status = session.test_fifo().wait_one(ZX_FIFO_READABLE, zx::time::infinite(), nullptr);
    ZX_ASSERT_OK(status, "wait FIFO readable");
    status = session.FetchDescriptors(returned_descriptors.begin(), buffer_count, &actual);
    ZX_ASSERT_OK(status, "failed to fetch descriptors");
    // Guarantee that all descriptors we sent come back to us, so the device can't be making any
    // work in its background threads.
    ZX_ASSERT_MSG(actual == buffer_count, "unexpected partial FIFO batch read %ld/%d", actual,
                  buffer_count);
  }

  sync_completion_t completion;
  device->Teardown([&completion]() { sync_completion_signal(&completion); });
  status = sync_completion_wait(&completion, zx::duration::infinite().get());
  ZX_ASSERT_OK(status, "sync_completion_wait(_, _) failed ");
  return true;
}

void RegisterTests() {
  constexpr uint16_t kBatchSizes[] = {1, 8, 16, 64, 256};
  for (auto& batch_size : kBatchSizes) {
    perftest::RegisterTest(fxl::StringPrintf("Latency/Rx/%d", batch_size).c_str(),
                           LatencyTest<RxTestSession>, batch_size);
    perftest::RegisterTest(fxl::StringPrintf("Latency/Tx/%d", batch_size).c_str(),
                           LatencyTest<TxTestSession>, batch_size);
  }
}
PERFTEST_CTOR(RegisterTests)

int main(int argc, char** argv) {
  constexpr char kTestSuiteName[] = "fuchsia.network.device";
  return perftest::PerfTestMain(argc, argv, kTestSuiteName);
}
