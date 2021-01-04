// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/flow_ring_handler.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/fake_msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/test_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/stub_netbuf.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Time out value for test waits.
static constexpr zx::duration kTestTimeout = zx::msec(100);

// Struct for holding on to all objects necessary to support a FlowRingHandler instance for test.
struct FlowRingHandlerAndDependencies {
  std::unique_ptr<FakeMsgbufInterfaces> fake_interfaces;
  std::unique_ptr<DmaPool> tx_buffer_pool;
  std::unique_ptr<FlowRingHandler> flow_ring_handler;
};

void CreateFlowRingHandlerAndDependencies(FlowRingHandlerAndDependencies* handler_and_deps) {
  ASSERT_EQ(ZX_OK, FakeMsgbufInterfaces::Create(&handler_and_deps->fake_interfaces));

  static constexpr size_t kDmaPoolBufferSize = 256;
  static constexpr int kDmaPoolBufferCount = 16;
  std::unique_ptr<DmaBuffer> dma_pool_buffer;
  ASSERT_EQ(ZX_OK, handler_and_deps->fake_interfaces->CreateDmaBuffer(
                       ZX_CACHE_POLICY_CACHED, kDmaPoolBufferSize * kDmaPoolBufferCount,
                       &dma_pool_buffer));
  ASSERT_EQ(ZX_OK, DmaPool::Create(kDmaPoolBufferSize, kDmaPoolBufferCount,
                                   std::move(dma_pool_buffer), &handler_and_deps->tx_buffer_pool));

  ASSERT_EQ(ZX_OK,
            FlowRingHandler::Create(handler_and_deps->fake_interfaces.get(),
                                    handler_and_deps->fake_interfaces->GetControlSubmitRing(),
                                    handler_and_deps->tx_buffer_pool.get(),
                                    &handler_and_deps->flow_ring_handler));
};

// Test creation of the FlowRingHandler using various creation parameters.
TEST(FlowRingHandlerTest, CreationParameters) {
  FlowRingHandlerAndDependencies handler_and_deps;
  CreateFlowRingHandlerAndDependencies(&handler_and_deps);
}

// Test interface creation and removal.
TEST(FlowRingHandlerTest, AddRemoveInterfaces) {
  FlowRingHandlerAndDependencies handler_and_deps;
  CreateFlowRingHandlerAndDependencies(&handler_and_deps);
  std::unique_ptr<FlowRingHandler> handler = std::move(handler_and_deps.flow_ring_handler);

  EXPECT_EQ(ZX_OK, handler->AddInterface(FlowRingHandler::InterfaceIndex{0}, true));
  EXPECT_EQ(ZX_OK, handler->AddInterface(FlowRingHandler::InterfaceIndex{1}, false));

  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, handler->AddInterface(FlowRingHandler::InterfaceIndex{0}, true));
  EXPECT_EQ(ZX_ERR_NOT_FOUND, handler->RemoveInterface(FlowRingHandler::InterfaceIndex{2}));

  EXPECT_EQ(ZX_OK, handler->RemoveInterface(FlowRingHandler::InterfaceIndex{0}));
  EXPECT_EQ(ZX_OK, handler->AddInterface(FlowRingHandler::InterfaceIndex{0}, false));
}

// Test flow ring creation and removal.
TEST(FlowRingHandlerTest, CreateDestroyFlowRings) {
  FlowRingHandlerAndDependencies handler_and_deps;
  CreateFlowRingHandlerAndDependencies(&handler_and_deps);
  std::unique_ptr<FlowRingHandler> handler = std::move(handler_and_deps.flow_ring_handler);
  static constexpr FlowRingHandler::InterfaceIndex kInterfaceIndex{0};
  static const wlan::common::MacAddr source_addr({31, 41, 52, 65, 35, 8});
  ASSERT_EQ(ZX_OK, handler->AddInterface(kInterfaceIndex, true));

  // Test creation of a flow ring.  Note that in AP mode, all multicast destinations get mapped to
  // MAC address FF:FF:FF:FF:FF.
  FlowRingHandler::RingIndex multicast_ring_index{0};
  uint16_t multicast_ring_id = 0;
  {
    static const wlan::common::MacAddr all_ff_mac({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    static const wlan::common::MacAddr source_addr({31, 41, 52, 65, 35, 8});
    static const wlan::common::MacAddr dest_addr({0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA});
    static const int kPriority = 1;
    sync_completion_t create_completed;
    ASSERT_TRUE(dest_addr.IsMcast());
    handler_and_deps.fake_interfaces->AddControlSubmitRingCallback(
        [&](const void* buffer, size_t size) {
          const auto request = GetMsgStruct<MsgbufFlowRingCreateRequest>(buffer, size);
          if (request == nullptr) {
            return;
          }
          EXPECT_EQ(0, std::memcmp(request->da, all_ff_mac.byte, sizeof(request->da)));
          EXPECT_EQ(0, std::memcmp(request->sa, source_addr.byte, sizeof(request->sa)));
          multicast_ring_id = request->flow_ring_id;
          sync_completion_signal(&create_completed);
        });
    EXPECT_EQ(ZX_OK, handler->GetOrAddFlowRing(kInterfaceIndex, source_addr, dest_addr, kPriority,
                                               &multicast_ring_index));
    sync_completion_wait(&create_completed, kTestTimeout.get());
    EXPECT_EQ(ZX_OK, handler->NotifyFlowRingCreated(multicast_ring_id, 0));

    // Expect that a second flow ring to the same destination returns the same ring.
    FlowRingHandler::RingIndex index_2;
    EXPECT_EQ(ZX_OK, handler->GetOrAddFlowRing(kInterfaceIndex, source_addr, dest_addr, kPriority,
                                               &index_2));
    EXPECT_EQ(multicast_ring_index.value, index_2.value);
  }

  // Test creation of a second, unicast flow ring.
  FlowRingHandler::RingIndex unicast_ring_index{0};
  uint16_t unicast_ring_id = 0;
  {
    static const wlan::common::MacAddr dest_addr({2, 71, 82, 81, 82, 84, 59});
    static const int kPriority = 1;
    sync_completion_t create_completed;
    ASSERT_TRUE(dest_addr.IsUcast());
    handler_and_deps.fake_interfaces->AddControlSubmitRingCallback(
        [&](const void* buffer, size_t size) {
          const auto request = GetMsgStruct<MsgbufFlowRingCreateRequest>(buffer, size);
          if (request == nullptr) {
            return;
          }
          EXPECT_EQ(0, std::memcmp(request->da, dest_addr.byte, sizeof(request->da)));
          EXPECT_EQ(0, std::memcmp(request->sa, source_addr.byte, sizeof(request->sa)));
          unicast_ring_id = request->flow_ring_id;
        });
    EXPECT_EQ(ZX_OK, handler->GetOrAddFlowRing(kInterfaceIndex, source_addr, dest_addr, kPriority,
                                               &unicast_ring_index));
    sync_completion_wait(&create_completed, kTestTimeout.get());
    EXPECT_EQ(ZX_OK, handler->NotifyFlowRingCreated(unicast_ring_id, 0));

    // Expect that a second flow ring to the same destination returns the same ring.
    FlowRingHandler::RingIndex index_2;
    EXPECT_EQ(ZX_OK, handler->GetOrAddFlowRing(kInterfaceIndex, source_addr, dest_addr, kPriority,
                                               &index_2));
    EXPECT_EQ(unicast_ring_index.value, index_2.value);
  }

  // Expect that interface removal removes the flow rings.
  {
    bool multicast_ring_deleted = false;
    bool unicast_ring_deleted = false;
    sync_completion_t delete_completion;
    auto flow_ring_delete_callback = [&](const void* buffer, size_t size) {
      const auto request = GetMsgStruct<MsgbufFlowRingDeleteRequest>(buffer, size);
      if (request == nullptr) {
        return;
      }
      if (request->flow_ring_id == multicast_ring_id) {
        EXPECT_FALSE(multicast_ring_deleted);
        multicast_ring_deleted = true;
      }
      if (request->flow_ring_id == unicast_ring_id) {
        EXPECT_FALSE(unicast_ring_deleted);
        unicast_ring_deleted = true;
      }
      if (multicast_ring_deleted && unicast_ring_deleted) {
        sync_completion_signal(&delete_completion);
      }
    };

    handler_and_deps.fake_interfaces->AddControlSubmitRingCallback(flow_ring_delete_callback);
    handler_and_deps.fake_interfaces->AddControlSubmitRingCallback(flow_ring_delete_callback);
    EXPECT_EQ(ZX_OK, handler->RemoveInterface(kInterfaceIndex));

    sync_completion_wait(&delete_completion, kTestTimeout.get());
    EXPECT_TRUE(multicast_ring_deleted);
    EXPECT_TRUE(unicast_ring_deleted);
    EXPECT_EQ(ZX_OK, handler->NotifyFlowRingDestroyed(multicast_ring_id, 0));
    EXPECT_EQ(ZX_OK, handler->NotifyFlowRingDestroyed(unicast_ring_id, 0));
  }
}

// Test buffer queueing and submission.
TEST(FlowRingHandlerTest, BufferQueueSubmit) {
  FlowRingHandlerAndDependencies handler_and_deps;
  CreateFlowRingHandlerAndDependencies(&handler_and_deps);
  std::unique_ptr<FlowRingHandler> handler = std::move(handler_and_deps.flow_ring_handler);
  static constexpr FlowRingHandler::InterfaceIndex kInterfaceIndex{1};
  FlowRingHandler::RingIndex ring_index{0};
  uint16_t flow_ring_id = 0;
  static const wlan::common::MacAddr source_addr({3, 141, 52, 65, 35, 85});
  static const wlan::common::MacAddr dest_addr({2, 71, 82, 81, 82, 84, 59});
  static const int kPriority = 1;
  ASSERT_EQ(ZX_OK, handler->AddInterface(kInterfaceIndex, true));

  // A lambda to install a flow ring callback waiting for data.
  auto add_flow_ring_callback = [&](const void* data, size_t data_size,
                                    sync_completion_t* complete) {
    handler_and_deps.fake_interfaces->AddFlowRingCallback(
        ring_index.value,
        [&handler_and_deps, data, data_size, complete](const void* buffer, size_t size) {
          const auto request = GetMsgStruct<MsgbufTxRequest>(buffer, size);
          if (request == nullptr) {
            return;
          }
          EXPECT_TRUE(ConcatenatedEquals(
              {{reinterpret_cast<const char*>(data), data_size}},
              {{reinterpret_cast<const char*>(request->txhdr), sizeof(request->txhdr)},
               {reinterpret_cast<const char*>(
                    handler_and_deps.fake_interfaces->GetDmaBufferAddress(request->data_buf_addr)),
                request->data_len}}));

          sync_completion_signal(complete);
        });
  };

  // Create the flow ring.
  sync_completion_t create_completed;
  handler_and_deps.fake_interfaces->AddControlSubmitRingCallback(
      [&](const void* buffer, size_t size) {
        const auto request = GetMsgStruct<MsgbufFlowRingCreateRequest>(buffer, size);
        if (request == nullptr) {
          return;
        }
        flow_ring_id = request->flow_ring_id;
        sync_completion_signal(&create_completed);
      });
  EXPECT_EQ(ZX_OK, handler->GetOrAddFlowRing(kInterfaceIndex, source_addr, dest_addr, kPriority,
                                             &ring_index));
  sync_completion_wait(&create_completed, kTestTimeout.get());

  // Submitting a Netbuf right after adding a flow ring has no effect yet.
  constexpr char kBeforeCreatedData[] = "This packet is before NotifyFlowRingCreated() was called.";
  EXPECT_EQ(ZX_OK,
            handler->Queue(ring_index, std::make_unique<StubNetbuf>(
                                           kBeforeCreatedData, sizeof(kBeforeCreatedData), ZX_OK)));
  handler->SubmitToFlowRings();

  // Now if we notify that the flow ring has been completed, we should see the Netbuf submitted.
  sync_completion_t before_created_data_complete;
  add_flow_ring_callback(kBeforeCreatedData, sizeof(kBeforeCreatedData),
                         &before_created_data_complete);
  handler->NotifyFlowRingCreated(flow_ring_id, 0);
  handler->SubmitToFlowRings();
  sync_completion_wait(&before_created_data_complete, kTestTimeout.get());

  // Submitting another Netbuf now happens immediately.
  constexpr char kAfterCreatedData[] = "This packet is after NotifyFlowRingCreated() was called.";
  EXPECT_EQ(ZX_OK,
            handler->Queue(ring_index, std::make_unique<StubNetbuf>(
                                           kAfterCreatedData, sizeof(kAfterCreatedData), ZX_OK)));
  sync_completion_t after_created_data_complete;
  add_flow_ring_callback(kAfterCreatedData, sizeof(kAfterCreatedData),
                         &after_created_data_complete);
  handler->SubmitToFlowRings();
  sync_completion_wait(&after_created_data_complete, kTestTimeout.get());

  // Queue another Netbuf, but remove the interface before submitting it; it should be returned with
  // ZX_ERR_CONNECTION_ABORTED.
  constexpr char kAfterRemovedData[] = "This packet is after the interface was removed.";
  EXPECT_EQ(ZX_OK, handler->Queue(ring_index, std::make_unique<StubNetbuf>(
                                                  kAfterRemovedData, sizeof(kAfterRemovedData),
                                                  ZX_ERR_CONNECTION_ABORTED)));
  handler->RemoveInterface(kInterfaceIndex);
  EXPECT_EQ(ZX_OK, handler->NotifyFlowRingDestroyed(flow_ring_id, 0));
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
