// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_INTERFACES_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_INTERFACES_H_

// This file contains interface definitions that are used to interact with the implementation of the
// MSGBUF protocol.

#include <zircon/types.h>

#include <memory>

namespace wlan {
namespace brcmfmac {

class DmaBuffer;
class ReadDmaRing;
class WriteDmaRing;

class DmaBufferProviderInterface {
 public:
  virtual ~DmaBufferProviderInterface();

  // Create a DMA buffer, suitable for use with the device.
  virtual zx_status_t CreateDmaBuffer(uint32_t cache_policy, size_t size,
                                      std::unique_ptr<DmaBuffer>* out_dma_buffer) = 0;
};

class DmaRingProviderInterface {
 public:
  // Firmware parameters associated with DMA usage.
  struct DmaConfig {
    int max_flow_rings;        // The maximum number of flow rings supported by the firmware.
    int flow_ring_offset;      // The firmware index of the first flow ring.  Firmware flow rings
                               // are indexed on the interval
                               //   [flow_ring_offset, flow_ring_offset + max_flow_rings);
    int max_ioctl_rx_buffers;  // The maximum number of idle RX buffers queued to firmware for ioctl
                               // calls.
    int max_event_rx_buffers;  // The maximum number of idle RX buffers queued to firmware for event
                               // notifications.
    int max_rx_buffers;        // The maximum number of idle RX buffers queued to firmware for
                               // wireless RX.
    size_t rx_data_offset;     // The offset to frame data in each received wireless RX buffer.
  };

  virtual ~DmaRingProviderInterface();

  // Get the configuration info.
  virtual const DmaConfig& GetDmaConfig() const = 0;

  // Get the specified rings.  These are static rings, and the DmaRingProviderInterface retains
  // ownership of the ring.
  virtual WriteDmaRing* GetControlSubmitRing() = 0;
  virtual WriteDmaRing* GetRxBufferSubmitRing() = 0;
  virtual ReadDmaRing* GetControlCompleteRing() = 0;
  virtual ReadDmaRing* GetTxCompleteRing() = 0;
  virtual ReadDmaRing* GetRxCompleteRing() = 0;

  // Create a flow ring.  These are dynamic rings, and the DmaRingProviderInterface does not retain
  // ownership of the ring.  Multiple instances of a flow ring may be created for the same flow ring
  // index; any such rings share the same (unsynchronized) underlying ring state.
  virtual zx_status_t CreateFlowRing(int flow_ring_index,
                                     std::unique_ptr<WriteDmaRing>* out_flow_ring) = 0;
};

class InterruptProviderInterface {
 public:
  // This is the interface for interrupt handlers that can be registered to the
  // InterruptProviderInterface.
  class InterruptHandler {
   public:
    virtual ~InterruptHandler();

    // The interrupt handler is invoked through this implementation.  The value of the interrupt
    // register will be passed in at the time of the interrupt, and the call should return the bits
    // of the register that will be masked off before the next handler is invoked.
    virtual uint32_t HandleInterrupt(uint32_t mailboxint) = 0;
  };

  virtual ~InterruptProviderInterface();

  // Add an interrupt handler to the provider, for the provider to invoke when an interrupt occurs.
  // Handlers will be invoked in the order in which they are added.  The handler added by this call
  // may be invoked before this call returns.
  virtual zx_status_t AddInterruptHandler(InterruptHandler* handler) = 0;

  // Remove a previously added interrupt handler.  The handler will not execute, and is guaranteed
  // not to be currently executing, once this call returns.
  virtual zx_status_t RemoveInterruptHandler(InterruptHandler* handler) = 0;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_INTERFACES_H_
