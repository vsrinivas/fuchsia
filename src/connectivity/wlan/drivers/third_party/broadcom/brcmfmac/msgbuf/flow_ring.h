// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_FLOW_RING_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_FLOW_RING_H_

#include <zircon/types.h>

#include <deque>
#include <memory>
#include <optional>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"

namespace wlan {
namespace brcmfmac {

// This class manages the state of one hardware flow ring, as provided by the brcmfmac firmware.
class FlowRing {
 public:
  FlowRing();
  FlowRing(const FlowRing& other) = delete;
  FlowRing(FlowRing&& other);
  FlowRing& operator=(FlowRing other);
  friend void swap(FlowRing& lhs, FlowRing& rhs);
  ~FlowRing();

  // Static factory function for FlowRing instances.  The created instance is valid, but not
  // considered open until NotifyOpen() is called.
  static zx_status_t Create(int interface_index, int flow_ring_index,
                            std::unique_ptr<WriteDmaRing> flow_dma_ring,
                            std::optional<FlowRing>* out_flow_ring);

  // Add a Netbuf to this transmit queue's queue.
  zx_status_t Queue(std::unique_ptr<Netbuf> netbuf);

  // Return true iff this FlowRing is ready and has Netbufs to submit.
  bool ShouldSubmit() const;

  // Submit as many buffers as possible from the queue into the flow ring.
  zx_status_t Submit(DmaPool* tx_buffer_pool, size_t max_submissions, size_t* out_submit_count);

  // Close this transmit queue.  The transmit queue is not completely closed until NotifyClosed() is
  // called.
  zx_status_t Close();

  // State update notifications.
  zx_status_t NotifyOpened();
  zx_status_t NotifyClosed();

  // State accessors.
  int interface_index() const;
  int flow_ring_index() const;

 private:
  // Transmit queue state machine states.
  enum class State {
    kInvalid = 0,
    kOpening = 1,
    kOpen = 2,
    kClosing = 3,
    kClosed = 4,
  };

  explicit FlowRing(uint8_t interface_index, uint16_t flow_ring_id,
                    std::unique_ptr<WriteDmaRing> flow_ring);

  State state_ = State::kInvalid;
  int interface_index_ = 0;
  int flow_ring_index_ = 0;
  std::unique_ptr<WriteDmaRing> flow_ring_;
  std::deque<std::unique_ptr<Netbuf>> netbuf_queue_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_FLOW_RING_H_
