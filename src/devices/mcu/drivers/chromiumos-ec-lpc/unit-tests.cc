// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/mcu/drivers/chromiumos-ec-lpc/chromiumos_ec_lpc.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace chromiumos_ec_lpc {

using inspect::InspectTestHelper;

class ChromiumosEcLpcTest;
ChromiumosEcLpcTest* kCurTest;
class ChromiumosEcLpcTest : public InspectTestHelper, public zxtest::Test {
 public:
  void SetUp() override {
    kCurTest = this;
    fake_root_ = MockDevice::FakeRootParent();
    io_buf_.resize(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_SIZE);

    // Set up the Fake EC far enough that we can bind the driver.
    acpi_memmap()[EC_MEMMAP_ID] = 'E';
    acpi_memmap()[EC_MEMMAP_ID + 1] = 'C';
    acpi_memmap()[EC_MEMMAP_HOST_CMD_FLAGS] = EC_HOST_CMD_FLAG_VERSION_3;

    // It's fine to leak this, because the mock ddk takes ownership of it.
    device_ = new ChromiumosEcLpc(fake_root_.get());
    ASSERT_OK(device_->Bind());
    device_->zxdev()->InitOp();
    ASSERT_OK(device_->zxdev()->WaitUntilInitReplyCalled(zx::time::infinite()));

    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_google_ec::Device>();
    ASSERT_OK(endpoints.status_value());
    client_.Bind(std::move(endpoints->client));
    ASSERT_OK(loop_.StartThread("cros-ec-lpc-test-fidl"));

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), device_);
  }

  void TearDown() override {
    device_->DdkAsyncRemove();
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_root_.get()));
  }

  uint8_t inb(uint16_t addr) { return io_buf_[addr]; }
  void outb(uint8_t data, uint16_t addr) {
    io_buf_[addr] = data;

    if (addr == EC_LPC_ADDR_HOST_CMD && data & EC_COMMAND_PROTOCOL_3) {
      // Copy request out of the buffer.
      std::vector<uint8_t> buf;
      auto* packet = reinterpret_cast<ec_host_request*>(packet_buf());
      buf.assign(packet_buf(), &packet_buf()[sizeof(*packet) + packet->data_len]);

      // Get a response.
      callback_(reinterpret_cast<ec_host_request*>(buf.data()), &buf[sizeof(ec_host_request)],
                &io_buf_[EC_LPC_ADDR_HOST_PACKET]);

      // Calculate checksum
      ec_host_response* response =
          reinterpret_cast<ec_host_response*>(&io_buf_[EC_LPC_ADDR_HOST_PACKET]);
      response->checksum = 0;
      size_t total_len = sizeof(*response) + response->data_len;

      int csum = 0;
      for (size_t i = 0; i < total_len; i++) {
        csum += packet_buf()[i];
      }

      response->checksum = static_cast<uint8_t>(-csum);
      // The EC clears these bits when the command has finished successfully.
      io_buf_[EC_LPC_ADDR_HOST_DATA] = 0;
      io_buf_[EC_LPC_ADDR_HOST_CMD] = 0;
    }
  }

  uint8_t* acpi_memmap() { return &io_buf_[EC_LPC_ADDR_MEMMAP]; }
  uint8_t* packet_buf() { return &io_buf_[EC_LPC_ADDR_HOST_PACKET]; }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  std::vector<uint8_t> io_buf_;
  std::function<void(ec_host_request* request, uint8_t* data, uint8_t* outbuf)> callback_;
  ChromiumosEcLpc* device_;
  fidl::WireSyncClient<fuchsia_hardware_google_ec::Device> client_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

}  // namespace chromiumos_ec_lpc

// IO port ops.

uint8_t WrappedInb(uint16_t addr) { return chromiumos_ec_lpc::kCurTest->inb(addr); }

void WrappedOutb(uint8_t data, uint16_t addr) { chromiumos_ec_lpc::kCurTest->outb(data, addr); }

zx_status_t zx_ioports_request(zx_handle_t resource, uint16_t io_addr, uint32_t len) {
  if (io_addr < EC_LPC_ADDR_ACPI_DATA) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if (io_addr > EC_LPC_ADDR_MEMMAP) {
    return ZX_ERR_ACCESS_DENIED;
  }
  return ZX_OK;
}

namespace chromiumos_ec_lpc {

TEST_F(ChromiumosEcLpcTest, LifetimeTest) {
  // Do nothing - the device is set up and torn down in the test fixture.
}

TEST_F(ChromiumosEcLpcTest, SendCommand) {
  callback_ = [](ec_host_request* request, uint8_t* data, uint8_t* outbuf) {
    ASSERT_EQ(request->struct_version, EC_HOST_REQUEST_VERSION);
    ASSERT_EQ(request->command, 0xaa);
    ASSERT_EQ(request->command_version, 0xbb);
    ASSERT_EQ(request->data_len, 2);
    ASSERT_EQ(data[0], 0x01);
    ASSERT_EQ(data[1], 0x23);

    ec_host_response* response = reinterpret_cast<ec_host_response*>(outbuf);
    response->struct_version = EC_HOST_RESPONSE_VERSION;
    response->reserved = 0;
    response->result = 0x1;
    response->data_len = 2;
    uint8_t* resp_data = outbuf + sizeof(*response);
    resp_data[0] = 0x11;
    resp_data[1] = 0x22;
  };

  uint8_t kData[2] = {0x01, 0x23};
  uint8_t kOutData[2] = {0x11, 0x22};
  auto result = client_->RunCommand(0xaa, 0xbb, fidl::VectorView<uint8_t>::FromExternal(kData, 2));
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->result.is_err(), "Failed: %s", zx_status_get_string(result->result.err()));
  ASSERT_EQ(int(result->result.response().result), 1);
  ASSERT_BYTES_EQ(result->result.response().data.data(), kOutData, 2);
}

}  // namespace chromiumos_ec_lpc
