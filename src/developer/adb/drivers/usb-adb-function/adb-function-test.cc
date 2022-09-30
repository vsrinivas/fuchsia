// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adb-function.h"

#include <fidl/fuchsia.hardware.adb/cpp/fidl.h>
#include <fuchsia/hardware/usb/function/cpp/banjo-mock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>

#include <map>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "usb/usb-request.h"

bool operator==(const usb_request_complete_callback_t& lhs,
                const usb_request_complete_callback_t& rhs) {
  // Comparison of these struct is not useful. Return true always.
  return true;
}

bool operator==(const usb_ss_ep_comp_descriptor_t& lhs, const usb_ss_ep_comp_descriptor_t& rhs) {
  // Comparison of these struct is not useful. Return true always.
  return true;
}

bool operator==(const usb_endpoint_descriptor_t& lhs, const usb_endpoint_descriptor_t& rhs) {
  // Comparison of these struct is not useful. Return true always.
  return true;
}

bool operator==(const usb_request_t& lhs, const usb_request_t& rhs) {
  // Only comparing endpoint address. Use ExpectCallWithMatcher for more specific
  // comparisons.
  return lhs.header.ep_address == rhs.header.ep_address;
}

bool operator==(const usb_function_interface_protocol_t& lhs,
                const usb_function_interface_protocol_t& rhs) {
  // Comparison of these struct is not useful. Return true always.
  return true;
}

namespace usb_adb_function {

typedef struct {
  usb_request_t* usb_request;
  const usb_request_complete_callback_t* complete_cb;
} mock_usb_request_t;

class MockUsbFunction : public ddk::MockUsbFunction {
 public:
  zx_status_t UsbFunctionCancelAll(uint8_t ep_address) override {
    while (!usb_request_queues[ep_address].empty()) {
      const mock_usb_request_t r = usb_request_queues[ep_address].back();
      r.complete_cb->callback(r.complete_cb->ctx, r.usb_request);
      usb_request_queues[ep_address].pop_back();
    }
    return ddk::MockUsbFunction::UsbFunctionCancelAll(ep_address);
  }

  zx_status_t UsbFunctionSetInterface(const usb_function_interface_protocol_t* interface) override {
    // Overriding method to store the interface passed.
    function = *interface;
    return ddk::MockUsbFunction::UsbFunctionSetInterface(interface);
  }

  zx_status_t UsbFunctionConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                  const usb_ss_ep_comp_descriptor_t* ss_comp_desc) override {
    // Overriding method to handle valid cases where nullptr is passed. The generated mock tries to
    // dereference it without checking.
    usb_endpoint_descriptor_t ep{};
    usb_ss_ep_comp_descriptor_t ss{};
    const usb_endpoint_descriptor_t* arg1 = ep_desc ? ep_desc : &ep;
    const usb_ss_ep_comp_descriptor_t* arg2 = ss_comp_desc ? ss_comp_desc : &ss;
    return ddk::MockUsbFunction::UsbFunctionConfigEp(arg1, arg2);
  }

  void UsbFunctionRequestQueue(usb_request_t* usb_request,
                               const usb_request_complete_callback_t* complete_cb) override {
    // Override to store requests.
    const uint8_t ep = usb_request->header.ep_address;
    auto queue = usb_request_queues.find(ep);
    if (queue == usb_request_queues.end()) {
      usb_request_queues[ep] = {};
    }
    usb_request_queues[ep].push_back({usb_request, complete_cb});
    mock_request_queue_.Call(*usb_request, *complete_cb);
  }

  usb_function_interface_protocol_t function;
  // Store request queues for each endpoint.
  std::map<uint8_t, std::vector<mock_usb_request_t>> usb_request_queues;
};

class UsbAdbTest : public zxtest::Test {
 public:
  static constexpr uint32_t kBulkOutEp = 1;
  static constexpr uint32_t kBulkInEp = 2;

  void SetUp() override {
    parent_ = MockDevice::FakeRootParent();
    parent_->AddProtocol(ZX_PROTOCOL_USB_FUNCTION, mock_usb_.GetProto()->ops,
                         mock_usb_.GetProto()->ctx);

    // Expect calls from UsbAdbDevice initialization
    mock_usb_.ExpectGetRequestSize(sizeof(usb_request_t));
    mock_usb_.ExpectAllocInterface(ZX_OK, 1);
    mock_usb_.ExpectAllocEp(ZX_OK, USB_DIR_OUT, kBulkOutEp);
    mock_usb_.ExpectAllocEp(ZX_OK, USB_DIR_IN, kBulkInEp);
    mock_usb_.ExpectSetInterface(ZX_OK, {});

    ASSERT_OK(UsbAdbDevice::Bind(nullptr, parent_.get()));

    adb_ = parent_->GetLatestChild()->GetDeviceContext<UsbAdbDevice>();
    ASSERT_NOT_NULL(adb_);

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_adb::Device>();
    ASSERT_TRUE(endpoints.is_ok());
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_OK(loop_->StartThread("usb-adb-test-loop"));
    fidl::BindServer<fidl::WireServer<fuchsia_hardware_adb::Device>>(
        loop_->dispatcher(), std::move(endpoints->server), adb_);
    client_ = std::move(endpoints->client);
  }

  void TearDown() override {
    mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
    mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
    mock_usb_.ExpectCancelAll(ZX_OK, kBulkOutEp);
    mock_usb_.ExpectCancelAll(ZX_OK, kBulkInEp);
    parent_->GetLatestChild()->SuspendNewOp(0, false, 0);
    parent_->GetLatestChild()->WaitUntilSuspendReplyCalled();
    loop_->Shutdown();
    parent_ = nullptr;
    mock_usb_.VerifyAndClear();
  }

 protected:
  UsbAdbDevice* adb_;
  MockUsbFunction mock_usb_;
  std::shared_ptr<MockDevice> parent_;
  std::unique_ptr<async::Loop> loop_;
  fidl::ClientEnd<fuchsia_hardware_adb::Device> client_;
};

// Fake Adb protocol service.
class FakeAdbDaemon : public fidl::WireAsyncEventHandler<fuchsia_hardware_adb::UsbAdbImpl> {
 public:
  explicit FakeAdbDaemon() : loop_(&kAsyncLoopConfigNeverAttachToThread) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_adb::UsbAdbImpl>();
    ASSERT_TRUE(endpoints.is_ok());
    client_ = fidl::WireSharedClient<fuchsia_hardware_adb::UsbAdbImpl>(std::move(endpoints->client),
                                                                       loop_.dispatcher(), this);
    server_ = std::move(endpoints->server);
  }

  fidl::ServerEnd<fuchsia_hardware_adb::UsbAdbImpl>&& GetServer() { return std::move(server_); }

  void OnStatusChanged(
      fidl::WireEvent<fuchsia_hardware_adb::UsbAdbImpl::OnStatusChanged>* event) override {
    status_ = event->status;
  }

  async::Loop& loop() { return loop_; }
  fuchsia_hardware_adb::StatusFlags status() { return status_; }
  fidl::WireSharedClient<fuchsia_hardware_adb::UsbAdbImpl>& client() { return client_; }

 private:
  fidl::WireSharedClient<fuchsia_hardware_adb::UsbAdbImpl> client_;
  fuchsia_hardware_adb::StatusFlags status_;
  async::Loop loop_;
  fidl::ServerEnd<fuchsia_hardware_adb::UsbAdbImpl> server_;
};

TEST_F(UsbAdbTest, SetUpTearDown) { ASSERT_NO_FATAL_FAILURE(); }

TEST_F(UsbAdbTest, StartStop) {
  fidl::WireSyncClient<fuchsia_hardware_adb::Device> adb_client(std::move(client_));
  auto fake_adb = FakeAdbDaemon();
  ASSERT_NO_FATAL_FAILURE();
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
  ASSERT_OK(adb_client->Start(std::move(fake_adb.GetServer())));

  // Calls during Stop().
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
}

TEST_F(UsbAdbTest, SendAdbMessage) {
  auto fake_adb = FakeAdbDaemon();
  ASSERT_NO_FATAL_FAILURE();

  // Start adb transactions.
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
  fidl::WireSyncClient<fuchsia_hardware_adb::Device> adb_client(std::move(client_));
  ASSERT_OK(adb_client->Start(std::move(fake_adb.GetServer())));

  // Call set_configured of usb adb to bring the interface online.
  mock_usb_.ExpectConfigEp(ZX_OK, {}, {});
  mock_usb_.ExpectConfigEp(ZX_OK, {}, {});
  const usb_header_t rx_header = {.ep_address = kBulkOutEp};
  for (uint32_t i = 0; i < kBulkRxCount; i++) {
    mock_usb_.ExpectRequestQueue({.header = rx_header}, {});
  }
  mock_usb_.function.ops->set_configured(mock_usb_.function.ctx, true, USB_SPEED_FULL);

  while (fake_adb.status() != fuchsia_hardware_adb::StatusFlags::kOnline) {
    EXPECT_EQ(ZX_OK, fake_adb.loop().RunUntilIdle());
  }

  // Queue transaction and check that the request is passed down the driver stack.
  uint8_t test_data[] = "test-data";
  mock_usb_.mock_request_queue().ExpectCallWithMatcher(
      [&](usb_request_t r, usb_request_complete_callback_t cb) -> void {
        ASSERT_EQ(r.header.ep_address, kBulkInEp);
        uint8_t got[32];
        const ssize_t length = usb_request_copy_from(&r, got, 32, 0);
        ASSERT_GT(length, 0);
        ASSERT_EQ(memcmp(test_data, got, sizeof(test_data)), 0);
      });
  ASSERT_OK(fake_adb.client().sync()->QueueTx(
      fidl::VectorView<uint8_t>::FromExternal(test_data, sizeof(test_data))));

  // Calls during Stop().
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
}

TEST_F(UsbAdbTest, RecvAdbMessage) {
  // Call set_configured of usb adb.
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
  mock_usb_.function.ops->set_configured(mock_usb_.function.ctx, true, USB_SPEED_FULL);

  auto fake_adb = FakeAdbDaemon();
  ASSERT_NO_FATAL_FAILURE();

  // Start adb transactions. This will also result in endpoint configuration.
  mock_usb_.ExpectConfigEp(ZX_OK, {}, {});
  mock_usb_.ExpectConfigEp(ZX_OK, {}, {});
  const usb_header_t header = {.ep_address = kBulkOutEp};
  for (uint32_t i = 0; i < kBulkRxCount; i++) {
    mock_usb_.ExpectRequestQueue({.header = header}, {});
  }

  fidl::WireSyncClient<fuchsia_hardware_adb::Device> adb_client(std::move(client_));
  ASSERT_OK(adb_client->Start(std::move(fake_adb.GetServer())));

  while (fake_adb.status() != fuchsia_hardware_adb::StatusFlags::kOnline) {
    EXPECT_EQ(ZX_OK, fake_adb.loop().RunUntilIdle());
  }

  // Queue a receive request before the data is available. The request will not get an immediate
  // reply. Queue a Receive request.
  const uint8_t test_data[] = "test-data";
  sync_completion_t completion;
  ASSERT_OK(fake_adb.loop().StartThread("adb-recv-thread"));
  fake_adb.client()->Receive().ThenExactlyOnce(
      [&](fidl::WireUnownedResult<::fuchsia_hardware_adb::UsbAdbImpl::Receive>& response) -> void {
        ASSERT_OK(response.status());
        ASSERT_FALSE(response.value().is_error());
        ASSERT_EQ(response.value().value()->data.count(), sizeof(test_data));
        ASSERT_BYTES_EQ(response.value().value()->data.data(), test_data, sizeof(test_data));
        sync_completion_signal(&completion);
      });
  // Invoke request completion on bulk out endpoint.
  const mock_usb_request_t req = mock_usb_.usb_request_queues[kBulkOutEp].back();
  mock_usb_.usb_request_queues[kBulkOutEp].pop_back();
  const ssize_t length = usb_request_copy_to(req.usb_request, test_data, sizeof(test_data), 0);
  ASSERT_GT(length, 0);
  req.usb_request->response.status = ZX_OK;
  req.usb_request->response.actual = sizeof(test_data);
  mock_usb_.ExpectRequestQueue({.header = header}, {});

  req.complete_cb->callback(req.complete_cb->ctx, req.usb_request);
  sync_completion_wait(&completion, zx::duration::infinite().get());

  // Calls during Stop().
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkOutEp);
  mock_usb_.ExpectDisableEp(ZX_OK, kBulkInEp);
}

}  // namespace usb_adb_function
