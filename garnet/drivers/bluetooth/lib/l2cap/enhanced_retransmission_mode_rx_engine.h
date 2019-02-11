// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_ENHANCED_RETRANSMISSION_MODE_RX_ENGINE_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_ENHANCED_RETRANSMISSION_MODE_RX_ENGINE_H_

#include <variant>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/l2cap/l2cap_internal.h"
#include "garnet/drivers/bluetooth/lib/l2cap/rx_engine.h"

namespace btlib {
namespace l2cap {
namespace internal {

// Implements the receiver state and logic for an L2CAP channel operating in
// Enhanced Retransmission Mode.
//
// THREAD-SAFETY: This class is not thread-safe.
class EnhancedRetransmissionModeRxEngine final : public RxEngine {
 public:
  EnhancedRetransmissionModeRxEngine();
  virtual ~EnhancedRetransmissionModeRxEngine() = default;

  common::ByteBufferPtr ProcessPdu(PDU) override;

 private:
  // See Core Spec v5, Vol 3, Part A, Sec 8.3.
  static constexpr auto kMaxSeqNum{63};

  common::ByteBufferPtr ProcessFrame(const SimpleInformationFrameHeader, PDU);
  common::ByteBufferPtr ProcessFrame(const SimpleStartOfSduFrameHeader, PDU);
  common::ByteBufferPtr ProcessFrame(const SimpleSupervisoryFrame, PDU);
  common::ByteBufferPtr ProcessFrame(std::monostate, PDU);
  void AdvanceSeqNum();

  // We assume that the Extended Window Size option is _not_ enabled. In such
  // cases, the sequence number is a 6-bit counter that wraps on overflow. See
  // Core Spec Ver 5, Vol 3, Part A, Secs 5.7 and 8.3.
  uint8_t next_seqnum_;  // (AKA Expected-TxSeq)

  FXL_DISALLOW_COPY_AND_ASSIGN(EnhancedRetransmissionModeRxEngine);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_ENHANCED_RETRANSMISSION_MODE_RX_ENGINE_H_
