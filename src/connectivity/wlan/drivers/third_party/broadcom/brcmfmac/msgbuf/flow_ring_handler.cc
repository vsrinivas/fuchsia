// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/flow_ring_handler.h"

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <optional>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"

namespace wlan {
namespace brcmfmac {
namespace {

// For fairness, we round-robin between all flow rings when performing TX buffer submission, to
// ensure that no one flow ring can monopolize all the available buffers.  This is the initial
// buffer count allocated for each flow ring during the initial round-robin pass; subsequent rounds
// (if buffers are still available) will have increasing allocations.
constexpr size_t kInitialTxBuffersPerFlowRing = 8;

// This is the maximum number of buffers submitted per flow ring, per round.
constexpr size_t kMaxTxBuffersPerFlowRing = 256;

// Each interface maintains a set of flow rings for transmitting data, one flow ring for each
// destination.
struct RingDestination {
  constexpr uint64_t AsUint64() const {
    return (static_cast<uint64_t>(destination.byte[0]) << 0) |
           (static_cast<uint64_t>(destination.byte[1]) << 8) |
           (static_cast<uint64_t>(destination.byte[2]) << 16) |
           (static_cast<uint64_t>(destination.byte[3]) << 24) |
           (static_cast<uint64_t>(destination.byte[4]) << 32) |
           (static_cast<uint64_t>(destination.byte[5]) << 40) | (static_cast<uint64_t>(fifo) << 48);
  }
  constexpr bool operator==(const RingDestination& other) const {
    return AsUint64() == other.AsUint64();
  }
  constexpr bool operator<(const RingDestination& other) const {
    return AsUint64() < other.AsUint64();
  }

  wlan::common::MacAddr destination = {};
  uint8_t fifo = 0;
};

// Map from a network priority to a FIFO index.  These values (and defaults) are used from the
// brcmfmac firmware.
static constexpr uint8_t kFifoList[] = {1, 0, 0, 1, 2, 2, 3, 3};
constexpr uint8_t GetFifoFromPriority(int priority) {
  if (priority < 0 || priority > static_cast<int>(sizeof(kFifoList) / sizeof(*kFifoList))) {
    return 1;
  }
  return kFifoList[priority];
}

// This is the set of structs possible in a flow ring entry.
union FlowRingEntry {
  MsgbufCommonHeader common_header;
  MsgbufTxRequest tx_request;
};

}  // namespace

// This struct tracks the flow rings associated with a particular interface.
struct FlowRingHandler::InterfaceState {
  std::map<RingDestination, RingIndex> ring_map;
  bool is_ap_mode = false;
};

// This struct tracks the state associated with a particular flow ring.
struct FlowRingHandler::FlowRingState
    : private intrusive_listable<FlowRingHandler::SubmitQueueTag> {
  friend class intrusive_list<FlowRingState, SubmitQueueTag>;
  FlowRing flow_ring;

  bool is_queued() const {
    return !static_cast<const intrusive_listable<SubmitQueueTag>&>(*this).empty();
  }
  void dequeue() { static_cast<intrusive_listable<SubmitQueueTag>&>(*this).erase(); }
};

FlowRingHandler::FlowRingHandler() = default;

FlowRingHandler::~FlowRingHandler() {
  for (size_t i = 0; i < interfaces_.size(); ++i) {
    if (interfaces_[i] != nullptr) {
      RemoveInterface(InterfaceIndex{static_cast<int>(i)});
    }
  }
  ZX_DEBUG_ASSERT(std::find_if(interfaces_.begin(), interfaces_.end(), [](const auto& value) {
                    return value != nullptr;
                  }) == interfaces_.end());
  // flow_rings_ may not yet be empty, since destruction of its individual instances wait on ring
  // notifications that will never arrive.
  ZX_DEBUG_ASSERT(submit_queue_.empty());
}

// static
zx_status_t FlowRingHandler::Create(DmaRingProviderInterface* dma_ring_provider,
                                    WriteDmaRing* control_submit_ring, DmaPool* tx_buffer_pool,
                                    std::unique_ptr<FlowRingHandler>* out_flow_ring_handler) {
  const size_t max_flow_rings = dma_ring_provider->GetDmaConfig().max_flow_rings;
  if (max_flow_rings <= 0) {
    BRCMF_ERR("Insufficient max_flow_rings %zu", max_flow_rings);
    return ZX_ERR_INVALID_ARGS;
  }

  auto flow_ring_handler = std::make_unique<FlowRingHandler>();

  flow_ring_handler->dma_ring_provider_ = dma_ring_provider;
  flow_ring_handler->control_submit_ring_ = control_submit_ring;
  flow_ring_handler->tx_buffer_pool_ = tx_buffer_pool;
  flow_ring_handler->flow_rings_.resize(max_flow_rings);

  *out_flow_ring_handler = std::move(flow_ring_handler);
  return ZX_OK;
}

// Add an InterfaceState to this FlowRingHandler to track the state of one interface.  The interface
// is initially created without any associated flow rings.
zx_status_t FlowRingHandler::AddInterface(InterfaceIndex interface_index, bool is_ap_mode) {
  if (static_cast<size_t>(interface_index.value) >= interfaces_.size()) {
    BRCMF_ERR("Invalid interface %d", interface_index.value);
    return ZX_ERR_INVALID_ARGS;
  }
  auto& interface = interfaces_[interface_index.value];
  if (interface != nullptr) {
    BRCMF_ERR("Interface %d already exists", interface_index.value);
    return ZX_ERR_ALREADY_BOUND;
  }

  interface = std::make_unique<InterfaceState>();
  interface->is_ap_mode = is_ap_mode;
  return ZX_OK;
}

// Remove an InterfaceState from this FlowRingHandler, disassociating any flow rings currently
// associated to this interface.
zx_status_t FlowRingHandler::RemoveInterface(InterfaceIndex interface_index) {
  zx_status_t status = ZX_OK;

  if (static_cast<size_t>(interface_index.value) >= interfaces_.size()) {
    BRCMF_ERR("Invalid interface %d", interface_index.value);
    return ZX_ERR_INVALID_ARGS;
  }
  auto& interface = interfaces_[interface_index.value];
  if (interface == nullptr) {
    // Not necessarily a reportable error.
    return ZX_ERR_NOT_FOUND;
  }

  for (auto& ring : interface->ring_map) {
    if ((status = CloseFlowRing(ring.second)) != ZX_OK) {
      BRCMF_ERR("Failed to close ring index %d: %s", ring.second.value,
                zx_status_get_string(status));
      // Keep going even if we fail here, to close out all the rings.
    }
  }
  interface.reset();
  return ZX_OK;
}

// Get the flow ring with the given destination and priority to from this interface, or add one if
// it doesn't already exist.  On success, the FlowRingState representing the ring is associated with
// this interface.  In the case that the ring was newly created, this does not yet mean that the
// firmware is aware of the ring; we receive confirmation of the creation through the receipt of
// NotifyFlowRingCreated().
zx_status_t FlowRingHandler::GetOrAddFlowRing(InterfaceIndex interface_index,
                                              const wlan::common::MacAddr& source,
                                              const wlan::common::MacAddr& destination,
                                              int priority, RingIndex* out_ring_index) {
  zx_status_t status = ZX_OK;
  if (static_cast<size_t>(interface_index.value) >= interfaces_.size()) {
    BRCMF_ERR("Invalid interface %d", interface_index.value);
    return ZX_ERR_INVALID_ARGS;
  }
  auto& interface = interfaces_[interface_index.value];
  if (interface == nullptr) {
    BRCMF_ERR("Interface %d not found", interface_index.value);
    return ZX_ERR_NOT_FOUND;
  }

  RingDestination dest = {destination, GetFifoFromPriority(priority)};
  if (interface->is_ap_mode && dest.destination.IsMcast()) {
    // Yeah, so the hardware likes to save on multicast rings, I suppose.
    dest.destination = wlan::common::MacAddr{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    dest.fifo = 0;
  }

  auto iter = interface->ring_map.lower_bound(dest);
  if (iter != interface->ring_map.end() && iter->first == dest) {
    *out_ring_index = iter->second;
    return ZX_OK;
  }

  // Notify the firmware that we are opening a flow ring.
  RingIndex ring_index;
  if ((status = OpenFlowRing(interface_index, source, dest.destination, dest.fifo, &ring_index)) !=
      ZX_OK) {
    return status;
  }

  // We add the flow ring to this interface's flow ring map, but until the firmware confirms receipt
  // using NotifyFlowRingCreated() we cannot yet actually use the ring.
  iter = interface->ring_map.emplace_hint(iter, dest, ring_index);
  *out_ring_index = iter->second;
  return ZX_OK;
}

// Queue a Netbuf to a flow ring for transmission.
zx_status_t FlowRingHandler::Queue(RingIndex ring_index, std::unique_ptr<Netbuf> netbuf) {
  zx_status_t status = ZX_OK;
  if (static_cast<size_t>(ring_index.value) >= flow_rings_.size()) {
    BRCMF_ERR("Invalid flow ring index %d", ring_index.value);
    netbuf->Return(ZX_ERR_INVALID_ARGS);
    return ZX_ERR_INVALID_ARGS;
  }
  auto& flow_ring_state = flow_rings_[ring_index.value];
  if (flow_ring_state == nullptr) {
    BRCMF_ERR("Flow ring index %d not found", ring_index.value);
    netbuf->Return(ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }
  if ((status = flow_ring_state->flow_ring.Queue(std::move(netbuf))) != ZX_OK) {
    BRCMF_ERR("Failed to queue flow ring index %d: %s", ring_index.value,
              zx_status_get_string(status));
    return status;
  }

  // If this flow ring has buffers to queue, and it is not already on the submit queue, then add the
  // flow ring to the submit queue.
  if (flow_ring_state->flow_ring.ShouldSubmit()) {
    if (!flow_ring_state->is_queued()) {
      submit_queue_.push_back(*flow_ring_state);
    }
  }

  return ZX_OK;
}

// Callback for a firmware notification that it has received the flow ring creation.
zx_status_t FlowRingHandler::NotifyFlowRingCreated(uint16_t flow_ring_id, int16_t ring_status) {
  const RingIndex ring_index{flow_ring_id - dma_ring_provider_->GetDmaConfig().flow_ring_offset};
  if (static_cast<size_t>(ring_index.value) >= flow_rings_.size()) {
    BRCMF_ERR("Invalid flow ring index %d", ring_index.value);
    return ZX_ERR_INVALID_ARGS;
  }
  auto& flow_ring_state = flow_rings_[ring_index.value];
  if (flow_ring_state == nullptr) {
    BRCMF_ERR("Flow ring index %d not found", ring_index.value);
    return ZX_ERR_NOT_FOUND;
  }

  const zx_status_t status = [&]() {
    zx_status_t status = ZX_OK;
    if (ring_status != 0) {
      BRCMF_ERR("Failed to create flow ring index %d: firmware status %d", ring_index.value,
                ring_status);
      return ZX_ERR_INTERNAL;
    }
    if ((status = flow_ring_state->flow_ring.NotifyOpened()) != ZX_OK) {
      BRCMF_ERR("Failed to open flow ring index %d: %s", ring_index.value,
                zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  }();
  if (status != ZX_OK) {
    // We're in a bad place, possibly bad or compromised firmware state.  Nuke the ring from orbit.
    // It's the only way to be sure.
    BRCMF_ERR("Terminating flow ring index %d: %s", ring_index.value, zx_status_get_string(status));
    TerminateFlowRingWithExtremePrejudice(ring_index);
    return status;
  }

  // Add to the submit queue if there are buffer to queue, and it's not already on the queue.
  if (flow_ring_state->flow_ring.ShouldSubmit()) {
    if (!flow_ring_state->is_queued()) {
      submit_queue_.push_back(*flow_ring_state);
    }
  }

  return ZX_OK;
}

// Callback for a firmware notification that it has received the flow ring deletion.
zx_status_t FlowRingHandler::NotifyFlowRingDestroyed(uint16_t flow_ring_id, int16_t ring_status) {
  const RingIndex ring_index{flow_ring_id - dma_ring_provider_->GetDmaConfig().flow_ring_offset};
  if (static_cast<size_t>(ring_index.value) >= flow_rings_.size()) {
    BRCMF_ERR("Invalid flow ring index %d", ring_index.value);
    return ZX_ERR_INVALID_ARGS;
  }
  auto& flow_ring_state = flow_rings_[ring_index.value];
  if (flow_ring_state == nullptr) {
    BRCMF_ERR("Flow ring index %d not found", ring_index.value);
    return ZX_ERR_NOT_FOUND;
  }

  const zx_status_t status = [&]() {
    zx_status_t status = ZX_OK;
    if (ring_status != 0) {
      BRCMF_ERR("Failed to destroy flow ring index %d: firmware status %d", ring_index.value,
                ring_status);
      return ZX_ERR_INTERNAL;
    }
    if ((status = flow_ring_state->flow_ring.NotifyClosed()) != ZX_OK) {
      BRCMF_ERR("Failed to notify closed flow ring index %d: %s", ring_index.value,
                zx_status_get_string(status));
      return status;
    }
    return ZX_OK;
  }();
  if (status != ZX_OK) {
    // We're in a bad place, possibly bad or compromised firmware state.  Nuke the ring from orbit.
    // It's the only way to be sure.
    BRCMF_ERR("Terminating flow ring index %d: %s", ring_index.value, zx_status_get_string(status));
    TerminateFlowRingWithExtremePrejudice(ring_index);
    return status;
  }

  flow_ring_state.reset();
  return ZX_OK;
}

void FlowRingHandler::SubmitToFlowRings() {
  // There have been some number of TX completions, which means that TX buffers are available.
  size_t tx_buffers_per_flow_ring = kInitialTxBuffersPerFlowRing;
  size_t submit_count = 0;

  while (!submit_queue_.empty()) {
    intrusive_list<FlowRingState, SubmitQueueTag> failure_submit_queue;
    intrusive_list<FlowRingState, SubmitQueueTag> pending_submit_queue;

    // Round-robin through all the flow rings with pending buffers, submitting up to
    // `tx_buffers_per_flow_ring` buffers each.
    do {
      FlowRingState& flow_ring_state = submit_queue_.front();
      size_t flow_ring_submit_count = 0;
      const zx_status_t status = flow_ring_state.flow_ring.Submit(
          tx_buffer_pool_, tx_buffers_per_flow_ring, &flow_ring_submit_count);
      submit_queue_.pop_front();

      if (status != ZX_OK) {
        // Some error occured; add this flow ring to the error queue.
        BRCMF_ERR("Flow ring interface %d index %d failed to submit: %s",
                  flow_ring_state.flow_ring.interface_index(),
                  flow_ring_state.flow_ring.flow_ring_index(), zx_status_get_string(status));
        failure_submit_queue.push_back(flow_ring_state);
      } else if (flow_ring_state.flow_ring.ShouldSubmit()) {
        // We have some buffers still pending.
        pending_submit_queue.push_back(flow_ring_state);
      }
      submit_count += flow_ring_submit_count;
    } while (!submit_queue_.empty());

    // The flow rings that had failures will try last next time.
    submit_queue_.splice(submit_queue_.end(), std::move(pending_submit_queue));
    submit_queue_.splice(submit_queue_.end(), std::move(failure_submit_queue));

    // If no buffers were successfully submitted this round, try again later.
    if (submit_count == 0) {
      break;
    }

    // Everybody has had a chance, so now we increase our allocation for the next iteration.
    tx_buffers_per_flow_ring = std::min(kMaxTxBuffersPerFlowRing, tx_buffers_per_flow_ring * 2);
  }
}

zx_status_t FlowRingHandler::OpenFlowRing(InterfaceIndex interface_index,
                                          const wlan::common::MacAddr& source,
                                          const wlan::common::MacAddr& destination, uint8_t fifo,
                                          RingIndex* out_ring_index) {
  zx_status_t status = ZX_OK;
  auto flow_ring_iter = std::find_if(flow_rings_.begin(), flow_rings_.end(),
                                     [](const auto& value) { return value == nullptr; });
  if (flow_ring_iter == flow_rings_.end()) {
    BRCMF_ERR("Failed to find free flow ring for interface_index %d", interface_index.value);
    return ZX_ERR_NO_RESOURCES;
  }

  const RingIndex ring_index =
      RingIndex{static_cast<int>(std::distance(flow_rings_.begin(), flow_ring_iter))};
  std::unique_ptr<WriteDmaRing> flow_dma_ring;
  if ((status = dma_ring_provider_->CreateFlowRing(ring_index.value, &flow_dma_ring)) != ZX_OK) {
    return status;
  }
  if (flow_dma_ring->item_size() < sizeof(FlowRingEntry)) {
    BRCMF_ERR("Failed to get flow dma ring index %d, too small: has %zu, requires %zu",
              ring_index.value, flow_dma_ring->item_size(), sizeof(FlowRingEntry));
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  const auto flow_ring_capacity = flow_dma_ring->capacity();
  const auto flow_ring_item_size = flow_dma_ring->item_size();
  const auto flow_ring_dma_address = flow_dma_ring->dma_address();

  std::optional<FlowRing> flow_ring;
  if ((status = FlowRing::Create(interface_index.value, ring_index.value, std::move(flow_dma_ring),
                                 &flow_ring)) != ZX_OK) {
    BRCMF_ERR("Failed to create flow ring index %d: %s", ring_index.value,
              zx_status_get_string(status));
    return status;
  }

  // Notify the firmware that we are opening the flow ring.
  {
    void* ring_buffer = nullptr;
    if ((status = control_submit_ring_->MapWrite(1, &ring_buffer)) != ZX_OK) {
      return status;
    }

    const auto flow_ring_create_request = new (ring_buffer) MsgbufFlowRingCreateRequest{};
    flow_ring_create_request->msg.msgtype = MsgbufFlowRingCreateRequest::kMsgType;
    flow_ring_create_request->msg.ifidx = interface_index.value;
    destination.CopyTo(flow_ring_create_request->da);
    source.CopyTo(flow_ring_create_request->sa);
    flow_ring_create_request->tid = fifo;
    flow_ring_create_request->flow_ring_id =
        ring_index.value + dma_ring_provider_->GetDmaConfig().flow_ring_offset;
    flow_ring_create_request->max_items = flow_ring_capacity;
    flow_ring_create_request->len_item = flow_ring_item_size;
    flow_ring_create_request->flow_ring_addr = flow_ring_dma_address;

    if ((status = control_submit_ring_->CommitWrite(1)) != ZX_OK) {
      return status;
    }
  }

  // Now create the FlowRing tracking the flow DMA ring, and the FlowRingState tracking the
  // FlowRing.
  *flow_ring_iter = std::make_unique<FlowRingState>();
  (*flow_ring_iter)->flow_ring = *std::move(flow_ring);

  *out_ring_index = ring_index;
  return ZX_OK;
}

zx_status_t FlowRingHandler::CloseFlowRing(RingIndex ring_index) {
  zx_status_t status = ZX_OK;
  ZX_DEBUG_ASSERT(static_cast<size_t>(ring_index.value) < flow_rings_.size());
  auto& flow_ring_state = flow_rings_[ring_index.value];
  if (flow_ring_state == nullptr) {
    BRCMF_ERR("Flow ring index %d not found", ring_index.value);
    return ZX_ERR_NOT_FOUND;
  }
  if ((status = flow_ring_state->flow_ring.Close()) != ZX_OK) {
    return status;
  }

  // Notify the firmware that we are closing the flow ring.
  {
    void* ring_buffer = nullptr;
    if ((status = control_submit_ring_->MapWrite(1, &ring_buffer)) != ZX_OK) {
      return status;
    }

    const auto flow_ring_delete_request = new (ring_buffer) MsgbufFlowRingDeleteRequest{};
    flow_ring_delete_request->msg.msgtype = MsgbufFlowRingDeleteRequest::kMsgType;
    flow_ring_delete_request->msg.ifidx = flow_ring_state->flow_ring.interface_index();
    flow_ring_delete_request->flow_ring_id = flow_ring_state->flow_ring.flow_ring_index() +
                                             dma_ring_provider_->GetDmaConfig().flow_ring_offset;

    if ((status = control_submit_ring_->CommitWrite(1)) != ZX_OK) {
      return status;
    }
  }

  flow_ring_state->dequeue();
  return ZX_OK;
}

void FlowRingHandler::TerminateFlowRingWithExtremePrejudice(RingIndex ring_index) {
  if (static_cast<size_t>(ring_index.value) >= flow_rings_.size()) {
    return;
  }

  // Remove from submit queue and flow ring vector.
  auto& flow_ring_state = flow_rings_[ring_index.value];
  flow_ring_state->dequeue();
  flow_ring_state.reset();

  // Remove from all interface maps.
  for (auto& interface : interfaces_) {
    if (interface != nullptr) {
      auto iter = interface->ring_map.begin();
      while (iter != interface->ring_map.end()) {
        if (iter->second.value == ring_index.value) {
          iter = interface->ring_map.erase(iter);
        } else {
          ++iter;
        }
      }
    }
  }
}

}  // namespace brcmfmac
}  // namespace wlan
