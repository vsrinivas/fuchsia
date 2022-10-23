// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_AUX_CHANNEL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_AUX_CHANNEL_H_

#include <lib/mmio/mmio.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

// Low-level channel configuration.
struct DdiAuxChannelConfig {
  // The transaction timeout period.
  int16_t timeout_us;

  // Total number of SYNC pulses sent when starting a transaction.
  //
  // The number includes the zeros sent as pre-charge pulses and the zeros at
  // the start of the AUX_SYNC pattern.
  //
  // The DisplayPort standard specifies 10-16 pre-charge pulses and 16
  // consecutive zeros at the start of the AUX_SYNC pattern.
  int8_t sync_pulse_count;

  // Number of SYNC pulses sent when starting a fast-wake transaction.
  //
  // The Embedded DisplayPort standard specifies 8 pulses.
  int8_t fast_wake_sync_pulse_count;

  // If true, transactions are directed through the Thunderbolt controller.
  // Otherwise, transactions are directed through the FIA.
  bool use_thunderbolt;
};

// Helper for accessing the DP AUX channel via the DDI registers.
//
// This class is focused on DDI register management. It has as little knowledge
// of the AUX channel protocol as needed to avoid unnecessary copying of the
// message data.
class DdiAuxChannel {
 public:
  // Commands take up a 4-bit field in the request header.
  static constexpr int32_t kMaxCommand = (1 << 4) - 1;

  // Addresses take up a 20-bit field in the request header.
  static constexpr int32_t kMaxAddress = (1 << 20) - 1;

  // AUX messages store the data length in a byte. However, the DP standard
  // limits AUX message data to 1-16 bytes, for both requests and replies.
  static constexpr int8_t kMaxOpSize = 16;

  // Metadata about a transaction request.
  struct Request {
    // The address in the request header. Must be between 0 and `kMaxAddress`.
    int32_t address;

    // The command in the request header. Must be between 0 and `kMaxCommand`.
    int8_t command;

    // The size of the operation, in bytes. Must be between 1 and `kMaxOpSize`.
    int8_t op_size;

    // The data payload attached to the request message.
    //
    // Write payloads should have the size stated in `op_size`. Read requests
    // have empty payloads.
    cpp20::span<const uint8_t> data;
  };

  // Metadata about a transaction reply.
  struct ReplyInfo {
    // The reply header byte.
    //
    // DdiAuxChannel methods do not validate this byte. According to the
    // DisplayPort specification, the upper 4 bits should be the reply command,
    // and the lower 4 bits should be zero.
    uint8_t reply_header;

    // Instances returned by DdiAuxChannel methods are guaranteed to have this
    // size between 0 and `kMaxOpSize`.
    int8_t reply_data_size;
  };

  // `mmio_buffer` must outlive this instance.
  DdiAuxChannel(fdf::MmioBuffer* mmio_buffer, tgl_registers::Ddi ddi, uint16_t device_id);

  // No copying.
  DdiAuxChannel(const DdiAuxChannel&) = delete;
  DdiAuxChannel& operator=(const DdiAuxChannel&) = delete;

  // Moving is allowed to facilitate storing in containers.
  DdiAuxChannel(DdiAuxChannel&&) noexcept = default;
  DdiAuxChannel& operator=(DdiAuxChannel&&) noexcept = default;

  // Performs an AUX transaction, exchanging one request and one reply message.
  //
  // `reply_data` points to a buffer populated with the data payload of the
  // reply message. If the buffer is smaller than the reply payload, only the
  // first `reply_data.size()` bytes of the payload are copied.
  //
  // The returned zx::result reflects whether the DDI considers this transaction
  // successful. If that's the case, the `ReplyInfo` structure has the reply
  // command byte (which could indicate a NACK or a DEFER), and the size of the
  // reply payload.
  zx::result<ReplyInfo> DoTransaction(const Request& request,
                                      cpp20::span<uint8_t> reply_data_buffer);

  // Directs AUX transactions to/away from the Thunderbolt controller.
  //
  // This method most only be called on Type C DDIs, to switch between
  // Thunderbolt connections and Type C (Alt Modes) connections.
  //
  // This method must not be called while a transaction is in progress.
  void SetUseThunderbolt(bool use_thunderbolt);

  // Reads the configuration in the cached control register.
  DdiAuxChannelConfig Config() const;

  // Outputs the current configuration as TRACE entries in the kernel log.
  void Log();

  // Stores an AUX channel request in the DDI's data buffer.
  //
  // This is a helper for DoTransaction(). It is only exposed for unit tests.
  //
  // After a request is stored in the DDI data buffer using this method,
  // Transact() should be used to transmit the request to the AUX channel.
  void WriteRequestForTesting(const Request& request);

  // Performs an AUX channel transaction, using a populated DDI data buffer.
  //
  // This is a helper for DoTransaction(). It is only exposed for unit tests.
  //
  // WriteRequest() must be called before this method.
  //
  // Returns a status that reflects whether the DDI considers the transaction
  // successful. If this method reports success, ReadReply() can be called to
  // retrieve the transaction reply. DDI-level success isn't conditioned on the
  // reply command, so NACKed and DEFERred transaction will still be considered
  // successful.
  zx::result<> TransactForTesting();

  // Reads an AUX channel response from the DDI's data buffer.
  //
  // This is a helper for DoTransaction(). It is only exposed for unit tests.
  //
  // Must only be called after a Transact() call that returns success.
  //
  // The response command byte is returned via ReplyInfo. The other response
  // bytes are copied into `data_buffer`. If the size of `data_buffer` is less
  // than the response, the buffer is filled with as many response bytes as
  // possible.
  //
  // Callers should pass a non-empty buffer even when performing a writes, so
  // they can retrieve the partial write size, if the write is NACKed.
  ReplyInfo ReadReplyForTesting(cpp20::span<uint8_t> data_buffer);

 private:
  // The DDI is set to time out after 1,600us (on Kaby Lake and Skylake) or
  // after 4,000us (on Tiger Lake and DG1). The timeout below gives the DDI a
  // large margin for reporting the timeout to us.
  static constexpr int kDdiTransactionTimeoutUs = 10'000;

  // Returns true if the transaction completes, and false if the wait timed out.
  bool WaitForTransactionComplete();

  // WriteRequest() helpers.
  void WriteRequestHeader(int8_t command, int32_t address, int8_t op_size);
  void WriteRequestData(cpp20::span<const uint8_t> data);

  // Patches up fields in the AUX control reg with obviously incorrect values.
  //
  // The fixes apply to the cached version of the AUX control register. The
  // caller is responsible for issuing a WriteTo() call to the register.
  void FixConfig();

  tgl_registers::DdiAuxControl aux_control_;

  fdf::MmioBuffer* mmio_buffer_;  // Non-null.

  // The value of large timeouts.
  int16_t large_timeout_us_;

#if ZX_DEBUG_ASSERT_IMPLEMENTED
  // The ZX_DEBUG_ASSERT_IMPLEMENTED block aims to clarify that these members
  // are only used for consistency checks. It is not intended as a performance
  // optimization.
  tgl_registers::Ddi ddi_;
  uint16_t device_id_;
#else

#if defined(ZX_DEBUG_ASSERT_IMPLEMENTED) && defined(ZX_ASSERT_LEVEL)
#error "ZX_ASSERT_LEVEL defined but evals to <= 1"
#else
#error "Else"
#endif

#endif  // ZX_DEBUG_ASSERT_IMPLEMENTED
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_AUX_CHANNEL_H_
