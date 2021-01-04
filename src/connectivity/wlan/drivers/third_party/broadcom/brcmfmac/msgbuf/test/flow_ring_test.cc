// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/flow_ring.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/fake_msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/test_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/stub_netbuf.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Time out value for test waits.
static constexpr zx::duration kTestTimeout = zx::msec(100);

// Sleep duration for test polling.  This a sleep interval purely for avoiding busy-wait, and does
// not affect test correctness.
static constexpr zx::duration kTestIterationSleep = zx::usec(100);

// Struct for holding on to all objects necessary to support a FlowRing instance for test.
struct FlowRingAndDependencies {
  std::unique_ptr<FakeMsgbufInterfaces> fake_interfaces;
  std::unique_ptr<DmaPool> tx_buffer_pool;
  int interface_index = 0;
  int flow_ring_index = 0;
  FlowRing flow_ring;
};

void CreateFlowRingAndDependencies(FlowRingAndDependencies* ring_and_deps) {
  ASSERT_EQ(ZX_OK, FakeMsgbufInterfaces::Create(&ring_and_deps->fake_interfaces));

  static constexpr size_t kDmaPoolBufferSize = 256;
  static constexpr int kDmaPoolBufferCount = 16;
  std::unique_ptr<DmaBuffer> dma_pool_buffer;
  ASSERT_EQ(ZX_OK, ring_and_deps->fake_interfaces->CreateDmaBuffer(
                       ZX_CACHE_POLICY_CACHED, kDmaPoolBufferSize * kDmaPoolBufferCount,
                       &dma_pool_buffer));
  ASSERT_EQ(ZX_OK, DmaPool::Create(kDmaPoolBufferSize, kDmaPoolBufferCount,
                                   std::move(dma_pool_buffer), &ring_and_deps->tx_buffer_pool));

  static constexpr uint16_t flow_ring_index = 3;
  std::unique_ptr<WriteDmaRing> flow_dma_ring;
  ASSERT_EQ(ZX_OK, ring_and_deps->fake_interfaces->CreateFlowRing(flow_ring_index, &flow_dma_ring));

  static constexpr int interface_index = 5;
  std::optional<FlowRing> flow_ring;
  ASSERT_EQ(ZX_OK, FlowRing::Create(interface_index, flow_ring_index, std::move(flow_dma_ring),
                                    &flow_ring));
  ASSERT_TRUE(flow_ring.has_value());
  ring_and_deps->interface_index = interface_index;
  ring_and_deps->flow_ring_index = flow_ring_index;
  ring_and_deps->flow_ring = *std::move(flow_ring);
};

// Test creation of the FlowRing.
TEST(FlowRingTest, CreationParameters) {
  FlowRingAndDependencies ring_and_deps;
  CreateFlowRingAndDependencies(&ring_and_deps);
  FlowRing flow_ring = std::move(ring_and_deps.flow_ring);

  EXPECT_EQ(ZX_OK, flow_ring.Close());
  EXPECT_EQ(ZX_OK, flow_ring.NotifyClosed());
}

// Test transmission of Netbufs.
TEST(FlowRingTest, NetbufTransmission) {
  FlowRingAndDependencies ring_and_deps;
  CreateFlowRingAndDependencies(&ring_and_deps);
  FlowRing flow_ring = std::move(ring_and_deps.flow_ring);

  // Queue some buffers for transmission, before we signal the flow ring open.
  using namespace std::string_literals;
  static const std::string kPreOpenTestData[] = {
      "This is a test buffer for the prequeue."s,
      "\x00\x01\x02\x03\x04\x05\x06\x07 blah blah blah blah blah blah"s,
      "foo foo foo foo foo \xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF"s,
      "<eth hdr size>\0"s,
      "<eth hdr size> "s,
  };
  for (size_t i = 0; i < countof(kPreOpenTestData); ++i) {
    const auto& test_data = kPreOpenTestData[i];
    ring_and_deps.fake_interfaces->AddFlowRingCallback(
        ring_and_deps.flow_ring_index, [&](const void* buffer, size_t size) {
          const auto tx_request = GetMsgStruct<MsgbufTxRequest>(buffer, size);
          if (tx_request == nullptr) {
            return;
          }
          EXPECT_EQ(ring_and_deps.interface_index, tx_request->msg.ifidx);
          EXPECT_TRUE(ConcatenatedEquals(
              {test_data},
              {{reinterpret_cast<const char*>(tx_request->txhdr), sizeof(tx_request->txhdr)},
               {reinterpret_cast<const char*>(
                    ring_and_deps.fake_interfaces->GetDmaBufferAddress(tx_request->data_buf_addr)),
                tx_request->data_len}}));
        });

    EXPECT_EQ(ZX_OK, flow_ring.Queue(
                         std::make_unique<StubNetbuf>(test_data.data(), test_data.size(), ZX_OK)));
  }

  // Signal the flow ring open.
  EXPECT_FALSE(flow_ring.ShouldSubmit());
  flow_ring.NotifyOpened();
  EXPECT_TRUE(flow_ring.ShouldSubmit());

  // Queue some more buffers for transmission, after we signal the flow ring open.
  sync_completion_t transmission_completed;
  static const std::string kPostOpenTestData[] = {
      "This is a test buffer for the postqueue."s,
      "\x00\x01\x02\x03\x04\x05\x06\x07 chat chat chat chat chat"s,
      "bar bar bar bar bar \xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF"s,
      "<eth hdr size>\0"s,
      "<eth hdr size>"s,
  };
  for (size_t i = 0; i < countof(kPostOpenTestData); ++i) {
    const auto& test_data = kPreOpenTestData[i];
    ring_and_deps.fake_interfaces->AddFlowRingCallback(
        ring_and_deps.flow_ring_index, [&, i](const void* buffer, size_t size) {
          const auto tx_request = GetMsgStruct<MsgbufTxRequest>(buffer, size);
          if (tx_request == nullptr) {
            return;
          }
          EXPECT_EQ(ring_and_deps.interface_index, tx_request->msg.ifidx);
          EXPECT_TRUE(ConcatenatedEquals(
              {test_data},
              {{reinterpret_cast<const char*>(tx_request->txhdr), sizeof(tx_request->txhdr)},
               {reinterpret_cast<const char*>(
                    ring_and_deps.fake_interfaces->GetDmaBufferAddress(tx_request->data_buf_addr)),
                tx_request->data_len}}));

          if (i == (countof(kPostOpenTestData) - 1)) {
            sync_completion_signal(&transmission_completed);
          }
        });

    EXPECT_EQ(ZX_OK, flow_ring.Queue(
                         std::make_unique<StubNetbuf>(test_data.data(), test_data.size(), ZX_OK)));
  }

  // Now submit all the buffers.
  // Note: we don't have buffer recycling set up in this test, so `tx_buffer_pool` must contain
  // enough buffers for all the test buffers we send.
  ASSERT_GE(static_cast<size_t>(ring_and_deps.tx_buffer_pool->buffer_count()),
            countof(kPreOpenTestData) + countof(kPostOpenTestData));
  static constexpr size_t kMaxSubmissionsPerIteration = 3;
  static constexpr int kMaxIterations = 1024;
  size_t submit_count = 0;
  for (int i = 0; i < kMaxIterations; ++i) {
    size_t iteration_submit_count = 0;
    EXPECT_EQ(ZX_OK, flow_ring.Submit(ring_and_deps.tx_buffer_pool.get(),
                                      kMaxSubmissionsPerIteration, &iteration_submit_count));
    submit_count += iteration_submit_count;
    if (submit_count == (countof(kPreOpenTestData) + countof(kPostOpenTestData))) {
      break;
    }
    zx::nanosleep(zx::deadline_after(kTestIterationSleep));
  }

  EXPECT_EQ(ZX_OK,
            sync_completion_wait(
                &transmission_completed,
                (kTestTimeout * (countof(kPreOpenTestData) + countof(kPostOpenTestData))).get()));
  EXPECT_EQ(ZX_OK, flow_ring.Close());
  EXPECT_EQ(ZX_OK, flow_ring.NotifyClosed());
}

// Test state machine transitions.
TEST(FlowRingTest, NetbufStateMachine) {
  // Transitions for a newly created FlowRing.
  {
    FlowRingAndDependencies ring_and_deps;
    CreateFlowRingAndDependencies(&ring_and_deps);
    FlowRing flow_ring = std::move(ring_and_deps.flow_ring);

    // Disallowed operations.
    size_t submit_count = 0;
    EXPECT_EQ(ZX_ERR_BAD_STATE,
              flow_ring.Submit(ring_and_deps.tx_buffer_pool.get(), 16, &submit_count));
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.NotifyClosed());

    // Allowed operations.
    auto netbuf = std::make_unique<StubNetbuf>(nullptr, 0, ZX_ERR_CONNECTION_ABORTED);
    EXPECT_EQ(ZX_OK, flow_ring.Queue(std::move(netbuf)));
    EXPECT_EQ(ZX_OK, flow_ring.NotifyOpened());

    // Put the flow ring into a good state for destruction.
    EXPECT_EQ(ZX_OK, flow_ring.Close());
    EXPECT_EQ(ZX_OK, flow_ring.NotifyClosed());
  }

  // Transitions for an open FlowRing.
  {
    FlowRingAndDependencies ring_and_deps;
    CreateFlowRingAndDependencies(&ring_and_deps);
    FlowRing flow_ring = std::move(ring_and_deps.flow_ring);
    EXPECT_EQ(ZX_OK, flow_ring.NotifyOpened());

    // Disallowed operations.
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.NotifyOpened());
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.NotifyClosed());

    // Allowed operations.
    auto netbuf = std::make_unique<StubNetbuf>(nullptr, 0, ZX_ERR_CONNECTION_ABORTED);
    EXPECT_EQ(ZX_OK, flow_ring.Queue(std::move(netbuf)));
    size_t submit_count = 0;
    EXPECT_EQ(ZX_OK, flow_ring.Submit(ring_and_deps.tx_buffer_pool.get(), 0, &submit_count));

    // Put the flow ring into a good state for destruction.
    EXPECT_EQ(ZX_OK, flow_ring.Close());
    EXPECT_EQ(ZX_OK, flow_ring.NotifyClosed());
  }

  // Transitions for a closing FlowRing.
  {
    FlowRingAndDependencies ring_and_deps;
    CreateFlowRingAndDependencies(&ring_and_deps);
    FlowRing flow_ring = std::move(ring_and_deps.flow_ring);
    EXPECT_EQ(ZX_OK, flow_ring.NotifyOpened());
    EXPECT_EQ(ZX_OK, flow_ring.Close());

    // Disallowed operations.
    auto netbuf = std::make_unique<StubNetbuf>(nullptr, 0, ZX_ERR_CONNECTION_ABORTED);
    EXPECT_EQ(ZX_ERR_CONNECTION_ABORTED, flow_ring.Queue(std::move(netbuf)));
    size_t submit_count = 0;
    EXPECT_EQ(ZX_ERR_BAD_STATE,
              flow_ring.Submit(ring_and_deps.tx_buffer_pool.get(), 16, &submit_count));

    // Allowed operations.
    EXPECT_EQ(ZX_OK, flow_ring.NotifyOpened());

    // Put the flow ring into a good state for destruction.
    EXPECT_EQ(ZX_OK, flow_ring.NotifyClosed());
  }

  // Transitiosn for a closed FlowRing.
  {
    FlowRingAndDependencies ring_and_deps;
    CreateFlowRingAndDependencies(&ring_and_deps);
    FlowRing flow_ring = std::move(ring_and_deps.flow_ring);
    EXPECT_EQ(ZX_OK, flow_ring.NotifyOpened());
    EXPECT_EQ(ZX_OK, flow_ring.Close());
    EXPECT_EQ(ZX_OK, flow_ring.NotifyClosed());

    // Disallowed operations.
    auto netbuf = std::make_unique<StubNetbuf>(nullptr, 0, ZX_ERR_BAD_STATE);
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.Queue(std::move(netbuf)));
    size_t submit_count = 0;
    EXPECT_EQ(ZX_ERR_BAD_STATE,
              flow_ring.Submit(ring_and_deps.tx_buffer_pool.get(), 0, &submit_count));
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.Close());
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.NotifyOpened());
    EXPECT_EQ(ZX_ERR_BAD_STATE, flow_ring.NotifyClosed());
  }
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
