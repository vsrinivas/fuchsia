// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_FLOW_RING_HANDLER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_FLOW_RING_HANDLER_H_

#include <zircon/types.h>

#include <array>
#include <memory>
#include <vector>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/intrusive_list.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/flow_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/lib/common/cpp/include/wlan/common/macaddr.h"

namespace wlan {
namespace brcmfmac {

// This class handles the brcmfmac DMA flowrings.  Each flowring is a TX queue intended for a single
// destination address.
//
// Thread-safety: this class is *not* thread-safe!  Access to this class must be externally
// synchronized.
class FlowRingHandler {
 public:
  // Declare the interface index type as a "strong typedef", to avoid ambiguity with the ring index.
  struct InterfaceIndex {
    static constexpr int kInvalid = -1;
    int value;
  };

  // Declare the ring index type as a "strong typedef", to avoid ambiguity with the interface index.
  struct RingIndex {
    static constexpr int kInvalid = -1;
    int value;
  };

  FlowRingHandler();
  ~FlowRingHandler();

  //
  // Public API
  //

  // Static factory function for FlowRingHandler instances.
  static zx_status_t Create(DmaRingProviderInterface* dma_ring_provider,
                            WriteDmaRing* control_submit_ring, DmaPool* tx_buffer_pool,
                            std::unique_ptr<FlowRingHandler>* out_flow_ring_handler);

  // Add or remove an interface.  When an interface is removed, all its currently associated flow
  // rings are disassociated.
  zx_status_t AddInterface(InterfaceIndex interface_index, bool is_ap_mode);
  zx_status_t RemoveInterface(InterfaceIndex interface_index);

  // Get the flow ring associated with an interface/destination/priority, adding a flow ring and
  // associating it with the interface/destination/priority if it doesn't already exist.
  zx_status_t GetOrAddFlowRing(InterfaceIndex interface_index, const wlan::common::MacAddr& source,
                               const wlan::common::MacAddr& destination, int priority,
                               RingIndex* out_ring_index);

  // Queue a Netbuf on a flow ring for transmission.
  zx_status_t Queue(RingIndex ring_index, std::unique_ptr<Netbuf> netbuf);

  //
  // Flow ring management API
  //

  // Notify the FlowRingHandler of hardware events.
  zx_status_t NotifyFlowRingCreated(uint16_t flow_ring_id, int16_t ring_status);
  zx_status_t NotifyFlowRingDestroyed(uint16_t flow_ring_id, int16_t ring_status);

  // Submit currently queued Netbufs to the flow rings, as far as possible.
  void SubmitToFlowRings();

 private:
  // Tag class for the submit queue.
  struct SubmitQueueTag {};

  // Internal interface state tracking.
  struct InterfaceState;

  // Internal flow ring state tracking.
  struct FlowRingState;

  // Open a flow ring.
  zx_status_t OpenFlowRing(InterfaceIndex interface_index, const wlan::common::MacAddr& source,
                           const wlan::common::MacAddr& destination, uint8_t fifo,
                           RingIndex* out_ring_index);

  // Close a flow ring.
  zx_status_t CloseFlowRing(RingIndex ring_index);

  // Heavyweight removal of a flow ring, by index.  Used on the error case to ensure consistency,
  // e.g. even if firmware is telling us junk.
  void TerminateFlowRingWithExtremePrejudice(RingIndex ring_index);

  // Hardware state.
  DmaRingProviderInterface* dma_ring_provider_ = nullptr;
  WriteDmaRing* control_submit_ring_ = nullptr;
  DmaPool* tx_buffer_pool_ = nullptr;

  // Internal state tracking.
  std::array<std::unique_ptr<InterfaceState>, BRCMF_MAX_IFS> interfaces_;
  std::vector<std::unique_ptr<FlowRingState>> flow_rings_;
  intrusive_list<FlowRingState, SubmitQueueTag> submit_queue_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_FLOW_RING_HANDLER_H_
