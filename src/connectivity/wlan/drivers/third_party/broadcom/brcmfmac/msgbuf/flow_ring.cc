// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/flow_ring.h"

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <algorithm>
#include <string>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Firmware frame flag definitions.
constexpr uint32_t BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_3 = 0x01;

// This is the set of structs possible in a flow ring entry.
union FlowRingEntry {
  MsgbufCommonHeader common_header;
  MsgbufTxRequest tx_request;
};

}  // namespace

// The transmit queue state machine is as follows:
//
// * State::kOpening: the FlowRing is initially created in this state.  FlowRing::Create() will post
//   a flow ring creation message to the firmware on the control submit ring.  Netbufs may be queued
//   in this state, but will not be submitted yet.
//   Allowed transitions:
//   -> State::kOpen: when NotifyOpened() is received.  The transmit queue is then open.
//   -> State::kClosing: when Close() called, in case the flow ring is closed before it was able to
//      fully open.
// * State::kOpen: the flow ring is open for business.  Netbufs that are queued in this state may be
//   submitted the next time Submit() is called.
//   Allowed transitions:
//   -> State::kClosing: when Close() is called.
// * State::kClosing: the flow ring is closing.  FlowRing::Close() will post a flow ring deletion
//   message to the firmware on the contorl submit ring.  Currently queued Netbufs, and any further
//   queuing, will return the Netbuf with ZX_ERR_CONNECTION_ABORTED.
//   Allowed transitions:
//   -> State::kClosed: when NotifyClosed() is received.
// * State::kClosed: the flow ring is closed.  No further transitions are permitted, and the
//   FlowRing is merely waiting for its destructor.
//   Allowed transitions:
//   -> <FlowRing destructor>

FlowRing::FlowRing() = default;

FlowRing::FlowRing(FlowRing&& other) { swap(*this, other); }

FlowRing& FlowRing::operator=(FlowRing other) {
  swap(*this, other);
  return *this;
}

void swap(FlowRing& lhs, FlowRing& rhs) {
  using std::swap;
  swap(lhs.state_, rhs.state_);
  swap(lhs.interface_index_, rhs.interface_index_);
  swap(lhs.flow_ring_index_, rhs.flow_ring_index_);
  swap(lhs.flow_ring_, rhs.flow_ring_);
  swap(lhs.netbuf_queue_, rhs.netbuf_queue_);
}

FlowRing::~FlowRing() {
  ZX_DEBUG_ASSERT(state_ == State::kInvalid || state_ == State::kClosed);
  if (!netbuf_queue_.empty()) {
    BRCMF_ERR("Destroying flow ring interface %d index %d with %zu remaining queued buffers",
              interface_index_, flow_ring_index_, netbuf_queue_.size());
  }
}

// static
zx_status_t FlowRing::Create(int interface_index, int flow_ring_index,
                             std::unique_ptr<WriteDmaRing> flow_dma_ring,
                             std::optional<FlowRing>* out_flow_ring) {
  if (flow_dma_ring->item_size() < sizeof(FlowRingEntry)) {
    BRCMF_ERR("Flow ring interface %d index %d too small: has %zu, requires %zu", interface_index,
              flow_ring_index, flow_dma_ring->item_size(), sizeof(FlowRingEntry));
    return ZX_ERR_NO_RESOURCES;
  }

  FlowRing flow_ring;
  flow_ring.state_ = State::kOpening;
  flow_ring.interface_index_ = interface_index;
  flow_ring.flow_ring_index_ = flow_ring_index;
  flow_ring.flow_ring_ = std::move(flow_dma_ring);

  *out_flow_ring = std::move(flow_ring);
  return ZX_OK;
}

zx_status_t FlowRing::Queue(std::unique_ptr<Netbuf> netbuf) {
  switch (state_) {
    case State::kOpening:
      [[fallthrough]];
    case State::kOpen:
      netbuf_queue_.emplace_back(std::move(netbuf));
      break;
    case State::kClosing: {
      netbuf->Return(ZX_ERR_CONNECTION_ABORTED);
      return ZX_ERR_CONNECTION_ABORTED;
    }
    default:
      BRCMF_ERR("Bad state %d for flow ring interface %d index %d", static_cast<int>(state_),
                interface_index_, flow_ring_index_);
      netbuf->Return(ZX_ERR_BAD_STATE);
      return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

bool FlowRing::ShouldSubmit() const { return state_ == State::kOpen && !netbuf_queue_.empty(); }

zx_status_t FlowRing::Submit(DmaPool* tx_buffer_pool, size_t max_submissions,
                             size_t* out_submit_count) {
  if (state_ != State::kOpen) {
    BRCMF_ERR("Bad state %d for flow ring interface %d index %d", static_cast<int>(state_),
              interface_index_, flow_ring_index_);
    return ZX_ERR_BAD_STATE;
  }

  size_t submit_count = 0;
  const zx_status_t status = [&]() {
    while (true) {
      zx_status_t status = ZX_OK;

      // Submit as many buffers as available entries in the flow ring, up to `max_submissions`.
      const size_t remaining_submits =
          std::min<size_t>(netbuf_queue_.size(), max_submissions - submit_count);
      const size_t entry_count =
          std::min<size_t>(remaining_submits, flow_ring_->GetAvailableWrites());
      if (entry_count == 0) {
        return ZX_OK;
      }

      void* ring_buffer = nullptr;
      if ((status = flow_ring_->MapWrite(entry_count, &ring_buffer)) != ZX_OK) {
        return status;
      }

      // Queue up to `entry_count` entries.
      size_t entries_queued = 0;
      status = [&]() {
        for (entries_queued = 0; entries_queued < entry_count; ++entries_queued) {
          // Get a TX buffer, and copy a Netbuf's worth of data into it.
          DmaPool::Buffer tx_buffer;
          if ((status = tx_buffer_pool->Allocate(&tx_buffer)) != ZX_OK) {
            if (status == ZX_ERR_NO_RESOURCES) {
              // This is fine, just try later.
              status = ZX_OK;
            }
            return status;
          }

          auto& netbuf = netbuf_queue_.front();
          static constexpr size_t kTxHeaderSize = sizeof(MsgbufTxRequest::txhdr);
          const size_t data_offset = std::min<size_t>(kTxHeaderSize, netbuf->size());
          const size_t data_size = netbuf->size() - data_offset;

          if (data_size > tx_buffer.size()) {
            BRCMF_ERR("Packet too large on flow ring interface %d index %d: req %zu, avail %zu",
                      interface_index_, flow_ring_index_, netbuf->size(),
                      kTxHeaderSize + tx_buffer.size());
            netbuf->Return(ZX_ERR_NO_RESOURCES);
            netbuf_queue_.pop_front();
            return ZX_ERR_NO_RESOURCES;
          }
          void* buffer = nullptr;
          if ((status = tx_buffer.MapWrite(netbuf->size(), &buffer)) != ZX_OK) {
            return status;
          }
          std::memcpy(buffer, reinterpret_cast<const char*>(netbuf->data()) + data_offset,
                      data_size);

          // Pin the TX buffer, then send it to the hardware.
          zx_paddr_t tx_buffer_dma_address = 0;
          if ((status = tx_buffer.Pin(&tx_buffer_dma_address)) != ZX_OK) {
            return status;
          }
          const auto tx_request =
              new (reinterpret_cast<char*>(ring_buffer) + entries_queued * flow_ring_->item_size())
                  MsgbufTxRequest{};
          tx_request->msg.msgtype = MsgbufTxRequest::kMsgType;
          tx_request->msg.request_id = tx_buffer.index();
          tx_request->msg.ifidx = interface_index_;
          tx_request->flags = BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_3;
          tx_request->seg_cnt = 1;
          std::memcpy(tx_request->txhdr, netbuf->data(), data_offset);
          tx_request->data_len = data_size;
          tx_request->data_buf_addr = tx_buffer_dma_address;

          tx_buffer.Release();  // The buffer is now owned by the hardware.
          netbuf->Return(ZX_OK);
          netbuf_queue_.pop_front();
        }
        return ZX_OK;
      }();

      if (status != ZX_OK) {
        BRCMF_ERR("Failed to write flow ring entry: %s", zx_status_get_string(status));
        if (entries_queued == 0) {
          // We don't propagate the error as commiting the ring writes up to here can still succeed.
          return ZX_OK;
        }
      }

      // Commit the entries that we did queue.
      if ((status = flow_ring_->CommitWrite(entries_queued)) != ZX_OK) {
        return status;
      }
      submit_count += entries_queued;
    }
  }();

  *out_submit_count = submit_count;
  return status;
}

zx_status_t FlowRing::Close() {
  switch (state_) {
    case State::kOpening:
      // It's possible that we are asking to close before the firmware has acknowledged that we are
      // fully open.
      [[fallthrough]];
    case State::kOpen:
      break;
    default:
      BRCMF_ERR("Bad state %d for flow ring interface %d index %d", static_cast<int>(state_),
                interface_index_, flow_ring_index_);
      return ZX_ERR_BAD_STATE;
  }

  state_ = State::kClosing;
  while (!netbuf_queue_.empty()) {
    netbuf_queue_.front()->Return(ZX_ERR_CONNECTION_ABORTED);
    netbuf_queue_.pop_front();
  }

  return ZX_OK;
}

zx_status_t FlowRing::NotifyOpened() {
  switch (state_) {
    case State::kOpening:
      break;
    case State::kClosing:
      // It's possible that we got asked to close before the firmware acknowledged that we were
      // fully open.
      return ZX_OK;
    default:
      BRCMF_ERR("Bad state %d for flow ring interface %d index %d", static_cast<int>(state_),
                interface_index_, flow_ring_index_);
      return ZX_ERR_BAD_STATE;
  }

  state_ = State::kOpen;
  return ZX_OK;
}

zx_status_t FlowRing::NotifyClosed() {
  switch (state_) {
    case State::kClosing:
      break;
    default:
      BRCMF_ERR("Bad state %d for flow ring interface %d index %d", static_cast<int>(state_),
                interface_index_, flow_ring_index_);
      return ZX_ERR_BAD_STATE;
  }

  state_ = State::kClosed;
  return ZX_OK;
}

int FlowRing::interface_index() const { return interface_index_; }

int FlowRing::flow_ring_index() const { return flow_ring_index_; }

}  // namespace brcmfmac
}  // namespace wlan
