// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_ring_handler.h"

#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/fake_msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/test_utils.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Time out value for test waits.
constexpr zx::duration kTestTimeout = zx::msec(100);

// Sleep duration when spinning and and retrying a test operation.
constexpr zx::duration kTestSpinSleep = zx::usec(100);

// Count and size of buffers.  We use small counts to force buffer recycling in tests.
constexpr int kPoolBufferSize = 1024;
constexpr size_t kPoolBufferCount = 32;

// Attempt multiple times to invoke a function.
template <typename F, typename... Args>
zx_status_t SpinInvoke(F&& f, Args&&... args) {
  zx_status_t status = ZX_OK;
  int iterations = 0;
  while ((status = std::invoke(std::forward<F>(f), std::forward<Args>(args)...)) ==
         ZX_ERR_SHOULD_WAIT) {
    if (iterations++ >= kTestTimeout / kTestSpinSleep) {
      return status;
    }
    zx::nanosleep(zx::deadline_after(kTestSpinSleep));
  }
  return status;
}

// Create a DMA pool.
void CreateDmaPool(FakeMsgbufInterfaces* interfaces, size_t buffer_size, int buffer_count,
                   std::unique_ptr<DmaPool>* dma_pool_out) {
  std::unique_ptr<DmaBuffer> dma_buffer;
  ASSERT_EQ(ZX_OK, interfaces->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, buffer_count * buffer_size,
                                               &dma_buffer));
  std::unique_ptr<DmaPool> dma_pool;
  ASSERT_EQ(ZX_OK, DmaPool::Create(buffer_size, buffer_count, std::move(dma_buffer), &dma_pool));
  *dma_pool_out = std::move(dma_pool);
}

// Test creation of the MsgbufRingHandler using various creation parameters.
TEST(MsgbufRingHandlerTest, CreationParameters) {
  std::unique_ptr<FakeMsgbufInterfaces> fake_interfaces;
  std::unique_ptr<DmaPool> rx_buffer_pool;
  std::unique_ptr<DmaPool> tx_buffer_pool;
  ASSERT_EQ(ZX_OK, FakeMsgbufInterfaces::Create(&fake_interfaces));
  CreateDmaPool(fake_interfaces.get(), kPoolBufferSize, kPoolBufferCount, &rx_buffer_pool);
  CreateDmaPool(fake_interfaces.get(), kPoolBufferSize, kPoolBufferCount, &tx_buffer_pool);

  std::unique_ptr<MsgbufRingHandler> ring_handler;
  EXPECT_EQ(ZX_OK, MsgbufRingHandler::Create(fake_interfaces.get(), fake_interfaces.get(),
                                             std::move(rx_buffer_pool), std::move(tx_buffer_pool),
                                             &ring_handler));
}

// Test the ioctl interfaces of the MsgbufRingHandler.  This test sends `kTestIterationCount`
// Ioctl() calls with set interface index, command, and data, expecting the bitwise negation of the
// data in return, and in order.
TEST(MsgbufRingHandlerTest, Ioctl) {
  struct IoctlTestData {
    uint8_t interface_index;
    uint32_t command;
    std::string data;
  };

  using namespace std::string_literals;
  IoctlTestData test_data[7] = {
      {0, 0, ""s},
      {42, 27, std::string(kPoolBufferSize, '\0')},
      {1, 2, "Lorem Ipsum"},
      {0xFF, ~0u, std::string(kPoolBufferSize / 2, '\x08')},
      {255, 65536, "foo\0bar\0baz\n\0"s},
      {3, 14, "159265358979323846264338327950288419716939937510"s},
      {0xFF, ~0u, std::string(kPoolBufferSize - 31, '\xFF')},
  };
  constexpr int kTestIterationCount = 256;

  std::unique_ptr<FakeMsgbufInterfaces> fake_interfaces;
  std::unique_ptr<DmaPool> rx_buffer_pool;
  std::unique_ptr<DmaPool> tx_buffer_pool;
  std::unique_ptr<MsgbufRingHandler> ring_handler;

  ASSERT_EQ(ZX_OK, FakeMsgbufInterfaces::Create(&fake_interfaces));
  CreateDmaPool(fake_interfaces.get(), kPoolBufferSize, kPoolBufferCount, &rx_buffer_pool);
  CreateDmaPool(fake_interfaces.get(), kPoolBufferSize, kPoolBufferCount, &tx_buffer_pool);
  ASSERT_EQ(ZX_OK, MsgbufRingHandler::Create(fake_interfaces.get(), fake_interfaces.get(),
                                             std::move(rx_buffer_pool), std::move(tx_buffer_pool),
                                             &ring_handler));

  // Set up the expectations for the control submit ring.
  for (int i = 0; i < kTestIterationCount; ++i) {
    const IoctlTestData& datum = test_data[i % countof(test_data)];

    // The operations we perform here should not require explicit synchronization with the Ioctl()
    // call itself, since the Ioctl() call should block on a zx::event until the response is
    // received.
    fake_interfaces->AddControlSubmitRingCallback([&](const void* data, size_t size) {
      // Make sure we received the ioctl request, as expected.
      const auto ioctl_request = GetMsgStruct<MsgbufIoctlRequest>(data, size);
      if (ioctl_request == nullptr) {
        return;
      }
      EXPECT_EQ(datum.interface_index, ioctl_request->msg.ifidx);
      EXPECT_EQ(datum.command, ioctl_request->cmd);
      EXPECT_EQ(datum.data.size(), ioctl_request->input_buf_len);
      const uintptr_t tx_buffer_address =
          fake_interfaces->GetDmaBufferAddress(ioctl_request->req_buf_addr);
      EXPECT_NE(0u, tx_buffer_address);

      // Now construct the expected response data, by bitwise NOT of all the byte data.
      auto buffer = fake_interfaces->GetIoctlRxBuffer();
      EXPECT_NE(0u, buffer.address);
      const size_t write_size = std::min<size_t>(ioctl_request->input_buf_len, buffer.size);
      if (tx_buffer_address != 0 && buffer.address != 0) {
        const char* tx_buffer_data = reinterpret_cast<const char*>(tx_buffer_address);
        char* rx_buffer_data = reinterpret_cast<char*>(buffer.address);
        for (size_t i = 0; i < write_size; ++i) {
          rx_buffer_data[i] = ~tx_buffer_data[i];
        }
      }

      // Send it back in a kIoctlResponse message.
      MsgbufIoctlResponse ioctl_response = {};
      ioctl_response.msg.msgtype = MsgbufIoctlResponse::kMsgType;
      ioctl_response.msg.request_id = buffer.index;
      ioctl_response.resp_len = write_size;
      ioctl_response.trans_id = ioctl_request->trans_id;
      ioctl_response.compl_hdr.status = ZX_OK;

      EXPECT_EQ(ZX_OK, SpinInvoke(&FakeMsgbufInterfaces::AddControlCompleteRingEntry,
                                  fake_interfaces.get(), &ioctl_response, sizeof(ioctl_response)));
    });
  }

  // Now perform the Ioctl() calls.  Each of these will complete sequentially, and should trigger
  // the responses we set up above.
  for (int i = 0; i < kTestIterationCount; ++i) {
    // Send the ioctl.
    const IoctlTestData& datum = test_data[i % countof(test_data)];
    DmaPool::Buffer tx_buffer;
    void* tx_buffer_data = nullptr;
    EXPECT_EQ(ZX_OK, ring_handler->GetTxBuffer(&tx_buffer));
    EXPECT_EQ(ZX_OK, tx_buffer.MapWrite(datum.data.size(), &tx_buffer_data));
    if (tx_buffer_data != nullptr) {
      std::memcpy(tx_buffer_data, datum.data.data(), datum.data.size());
    }

    // Confirm receipt of the response.
    DmaPool::Buffer rx_buffer;
    const void* rx_buffer_data = nullptr;
    size_t rx_data_size = 0;
    int32_t firmware_error = 0;
    EXPECT_EQ(ZX_OK, ring_handler->Ioctl(datum.interface_index, datum.command, std::move(tx_buffer),
                                         datum.data.size(), &rx_buffer, &rx_data_size,
                                         &firmware_error, kTestTimeout));
    EXPECT_EQ(ZX_OK, rx_buffer.MapRead(rx_data_size, &rx_buffer_data));
    EXPECT_EQ(0, firmware_error);

    // Confirm the response.
    EXPECT_EQ(datum.data.size(), rx_data_size);
    const size_t compare_size = std::min<size_t>(datum.data.size(), rx_data_size);
    const char* rx_buffer_data_char = reinterpret_cast<const char*>(rx_buffer_data);
    for (size_t i = 0; i < compare_size; ++i) {
      EXPECT_EQ(static_cast<char>(~datum.data[i]), rx_buffer_data_char[i]);
    }
  }
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
