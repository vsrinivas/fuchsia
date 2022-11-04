// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-aux-channel.h"

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

DdiAuxChannel::DdiAuxChannel(fdf::MmioBuffer* mmio_buffer, DdiId ddi_id, uint16_t device_id)
    : mmio_buffer_(mmio_buffer), large_timeout_us_(0) {
  ZX_ASSERT(mmio_buffer);

  if (is_skl(device_id) || is_kbl(device_id)) {
    aux_control_ = tgl_registers::DdiAuxControl::GetForKabyLakeDdi(ddi_id).ReadFrom(mmio_buffer);
    large_timeout_us_ = 1'600;
  } else if (is_tgl(device_id)) {
    aux_control_ = tgl_registers::DdiAuxControl::GetForTigerLakeDdi(ddi_id).ReadFrom(mmio_buffer);
    large_timeout_us_ = 4'000;
  } else if (is_test_device(device_id)) {
    // Stubbed for integration tests.
  } else {
    ZX_ASSERT_MSG(false, "Unsupported device ID %x", device_id);
  }

#if ZX_DEBUG_ASSERT_IMPLEMENTED
  ddi_id_ = ddi_id;
  device_id_ = device_id;
#endif  // ZX_DEBUG_ASSERT_IMPLEMENTED

  if (aux_control_.transaction_in_progress()) {
    // The boot firmware kicked off an AUX transaction and handed off control to
    // the OS without waiting for the transaction to complete.
    zxlogf(WARNING, "DDI %u AUX channel initialization blocked by pre-existing transaction.",
           ddi_id);

    // It's tempting to consider adjusting the AUX parameters below waiting for
    // the transaction to complete. However, we're not allowed to write the AUX
    // control register while the `transaction_in_progress` bit is set.
    if (!WaitForTransactionComplete()) {
      // All future transactions will most likely fail. Soldier on and hope the
      // DDI miraculously fixes itself.
      zxlogf(ERROR,
             "DDI %u AUX channel initialization wait for pre-existing transaction timed out.",
             ddi_id);
    }
  }
}

zx::result<DdiAuxChannel::ReplyInfo> DdiAuxChannel::DoTransaction(
    const Request& request, cpp20::span<uint8_t> reply_data_buffer) {
  WriteRequestForTesting(request);
  zx::result<> transact_status = TransactForTesting();
  if (!transact_status.is_ok()) {
    return transact_status.take_error();
  }
  return zx::ok(ReadReplyForTesting(reply_data_buffer));
}

void DdiAuxChannel::SetUseThunderbolt(bool use_thunderbolt) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  if (use_thunderbolt) {
    ZX_DEBUG_ASSERT(is_tgl(device_id_));
    ZX_DEBUG_ASSERT(ddi_id_ >= DdiId::DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi_id_ <= DdiId::DDI_TC_6);
  }
#endif  //  ZX_DEBUG_ASSERT_IMPLEMENTED

  aux_control_.set_use_thunderbolt(use_thunderbolt);
}

DdiAuxChannelConfig DdiAuxChannel::Config() const {
  int16_t timeout_us;
  switch (aux_control_.timeout_timer_select()) {
    case tgl_registers::DdiAuxControl::kTimeoutUnsupported400us:
      timeout_us = 400;
      break;
    case tgl_registers::DdiAuxControl::kTimeout600us:
      timeout_us = 600;
      break;
    case tgl_registers::DdiAuxControl::kTimeout800us:
      timeout_us = 800;
      break;
    case tgl_registers::DdiAuxControl::kTimeoutLarge:
      timeout_us = large_timeout_us_;
      break;
  }

  // The casts are lossless because the pulse counts are 5-bit fields.
  const int8_t raw_fast_wake_sync_pulse_count =
      static_cast<int8_t>(aux_control_.fast_wake_sync_pulse_count());
  const int8_t raw_sync_pulse_count = static_cast<int8_t>(aux_control_.sync_pulse_count());

  // The additions and casts do not overflow (which would be UB) because raw
  // pulse counts are between 0 and 31.
  const int8_t fast_wake_sync_pulse_count = static_cast<int8_t>(raw_fast_wake_sync_pulse_count + 1);
  const int8_t sync_pulse_count = static_cast<int8_t>(raw_sync_pulse_count + 1);

  return {
      .timeout_us = timeout_us,
      .sync_pulse_count = sync_pulse_count,
      .fast_wake_sync_pulse_count = fast_wake_sync_pulse_count,

      // The cast is lossless because use_thunderbolt() is a 1-bit field.
      .use_thunderbolt = static_cast<bool>(aux_control_.use_thunderbolt()),
  };
}

void DdiAuxChannel::Log() {
  const DdiAuxChannelConfig config = Config();
  zxlogf(TRACE, "Timeout: %d us", config.timeout_us);
  zxlogf(TRACE, "SYNC pulses: %d standard, %d fast wake", config.sync_pulse_count,
         config.fast_wake_sync_pulse_count);
  zxlogf(TRACE, "Use Thunderbolt: %s", config.use_thunderbolt ? "yes" : "no");
  zxlogf(TRACE, "DDI_AUX_CTL: %x", aux_control_.reg_value());
}

void DdiAuxChannel::WriteRequestForTesting(const Request& request) {
  WriteRequestHeader(request.command, request.address, request.op_size);
  WriteRequestData(request.data);

  // Transact() will call WriteTo() after setting more fields.
  aux_control_.set_message_size(4 + request.data.size());
}

bool DdiAuxChannel::WaitForTransactionComplete() {
  return PollUntil(
      [&] {
        aux_control_.ReadFrom(mmio_buffer_);
        // Wait for transaction_in_progress() to be cleared, so we know we're
        // allowed to write to the AUX control register. Also wait for
        // transaction_done() to be set, so we know we'll get meaningful results
        // when we read the AUX data registers.
        return !aux_control_.transaction_in_progress() && aux_control_.transaction_done();
      },
      zx::usec(1), kDdiTransactionTimeoutUs);
}

void DdiAuxChannel::WriteRequestHeader(int8_t command, int32_t address, int8_t op_size) {
  ZX_ASSERT(command >= 0);
  ZX_ASSERT(command <= kMaxCommand);
  ZX_ASSERT(address >= 0);
  ZX_ASSERT(address <= kMaxAddress);

  // For now, we don't handle zero-byte operations. (However, they can be used
  // for checking whether there is an I2C device at a given address.)
  ZX_ASSERT(op_size > 0);

  ZX_ASSERT(op_size <= kMaxOpSize);

  const uint8_t byte0 = static_cast<uint8_t>((command << 4) | (address >> 16));
  const uint8_t byte1 = static_cast<uint8_t>(address >> 8);
  const uint8_t byte2 = static_cast<uint8_t>(address);
  const uint8_t byte3 = static_cast<uint8_t>(op_size - 1);

  // The most significant byte in each 32-bit register gets transmitted first.
  // Intel machines are little-endian, so the transmission order doesn't match
  // the memory order.
  //
  // The compiler will optimize away redundant shifts.
  const uint32_t swapped_bytes =
      (uint32_t{byte0} << 24) | (uint32_t{byte1} << 16) | (uint32_t{byte2} << 8) | uint32_t{byte3};

  auto aux_data_header =
      tgl_registers::DdiAuxData::GetData0ForAuxControl(aux_control_).FromValue(0);
  aux_data_header.set_swapped_bytes(swapped_bytes).WriteTo(mmio_buffer_);
}

void DdiAuxChannel::WriteRequestData(cpp20::span<const uint8_t> data) {
  ZX_ASSERT(data.size() <= kMaxOpSize);

  // Points to the data byte currently copied into the AUX DDI buffer.
  const uint8_t* data_pointer = data.data();

  // Points 4 bytes below the MMIO address of the AUX DDI buffer being written to.
  zx_off_t aux_data_mmio_address =
      tgl_registers::DdiAuxData::GetData0ForAuxControl(aux_control_).addr();

  // The cast is lossless because data.size() is at most 16.
  int data_left = static_cast<int>(data.size());

  while (data_left > 0) {
    uint32_t swapped_bytes;
    if (data_left >= 4) {
      // Fast path. This gets optimized to one `bswap` instruction.
      swapped_bytes = (uint32_t{data_pointer[0]} << 24) | (uint32_t{data_pointer[1]} << 16) |
                      (uint32_t{data_pointer[2]} << 8) | uint32_t{data_pointer[3]};
      data_left -= 4;

      data_pointer += 4;
    } else {
      uint8_t raw_bytes[] = {0, 0, 0, 0};
      std::memcpy(&raw_bytes[0], data_pointer, data_left);
      swapped_bytes = (uint32_t{raw_bytes[0]} << 24) | (uint32_t{raw_bytes[1]} << 16) |
                      (uint32_t{raw_bytes[2]} << 8) | uint32_t{raw_bytes[3]};
      data_left = 0;

      // We don't need to update `data_pointer` on the slow path because we'll
      // exit the loop. Adding 4 here would yield undefined behavior, because it
      // would get the pointer past the buffer it points to.
    }

    aux_data_mmio_address += 4;
    mmio_buffer_->Write32(swapped_bytes, aux_data_mmio_address);
  }
}

zx::result<> DdiAuxChannel::TransactForTesting() {
  // If the AUX control register works as documented, it should be sufficient to
  // call FixConfig() once, to adjust the configuration left over from the boot
  // firmware.
  //
  // Calling FixConfig() every transaction ensures the configuration is still
  // what we expect even if the control register's configuration fields changed
  // while we were reading it in a previous execution of Transact().
  FixConfig();

  // Resets the R/WC (Read/Write-Clear) indicators. This guarantees the
  // indicators are meaningful when the transaction completes.
  aux_control_.set_transaction_done(true).set_timeout(true).set_receive_error(true);

  // Setting this field kicks off the transaction. The write also picks up the
  // `message_size` field change done in WriteRequest().
  aux_control_.set_transaction_in_progress(true).WriteTo(mmio_buffer_);

  if (!WaitForTransactionComplete()) {
    // The DDI did not complete the transaction (which includes reporting an AUX
    // timeout) in the allotted time. This is most likely a hardware error.
    zxlogf(WARNING, "DDI did not complete / fail AUX transaction in %d us",
           kDdiTransactionTimeoutUs);
    return zx::make_result(ZX_ERR_IO_MISSED_DEADLINE);
  }

  if (aux_control_.timeout()) {
    // AUX timeouts are expected for slow devices, so this condition does not
    // warrant serious logging.
    //
    // For example, the maximum AUX timeout supported by Kaby Lake and Skylake
    // is 1,600us but, since DisplayPort 1.4a, sinks are allowed 3,200us (3.2ms)
    // to reply to AUX transactions right after the hot-plug detect event, and
    // when woken up from a low power state.
    //
    // The 3.2ms timeout comes from the DisplayPort 2.0 standard version 2.0,
    // section 2.11.2 "AUX Trransaction Response/Reply Timeouts", page 382.
    zxlogf(TRACE, "DDI reported AUX transaction timeout. This is normal after HPD or wakeup.");
    return zx::make_result(ZX_ERR_IO_MISSED_DEADLINE);
  }
  if (aux_control_.receive_error()) {
    zxlogf(WARNING, "DDI AUX receive error. Data corrupted or incorrect bit count.");
    return zx::make_result(ZX_ERR_IO_DATA_INTEGRITY);
  }

  // The cast is lossless because message_size() is a 5-bit field.
  const int reply_size = static_cast<int>(aux_control_.message_size());

  // AUX replies must contain at least one command byte. AUX replies can contain
  // at most 16 data bytes, asides from the header byte.
  if (reply_size == 0 || reply_size > 1 + kMaxOpSize) {
    zxlogf(WARNING, "DDI AUX invalid reply size: %d bytes", reply_size);
    return zx::make_result(ZX_ERR_IO_DATA_INTEGRITY);
  }

  return zx::ok();
}

// Reads an AUX channel response from the DDI's data buffer.
DdiAuxChannel::ReplyInfo DdiAuxChannel::ReadReplyForTesting(cpp20::span<uint8_t> data_buffer) {
  // We rely on the fact that Transact() must have done an MMIO read for
  // `aux_control_` before exiting successfully.
  //
  // Transact() would not have returned success if any of these predicates is
  // false.
  ZX_ASSERT(!aux_control_.transaction_in_progress());
  ZX_ASSERT(!aux_control_.receive_error());
  ZX_ASSERT(!aux_control_.timeout());
  ZX_ASSERT(aux_control_.transaction_done());
  ZX_ASSERT(aux_control_.message_size() >= 1);

  // The cast is lossless because message_size() is a 5-bit field.
  const int8_t aux_message_size = static_cast<int8_t>(aux_control_.message_size());
  // The cast is lossless because `aux_message_size` is between 0 and 31. Also,
  // we checked above that `aux_message_size` is at least 1.
  const int8_t aux_data_size = static_cast<int8_t>(aux_message_size - 1);

  // The cast is lossless because the min() result is at most `aux_data_size`,
  // which is is at most 31.
  int data_left = static_cast<int>(std::min<size_t>(aux_data_size, data_buffer.size()));

  // The first AUX data register is a special case, because it contains the
  // headear byte.
  auto aux_data_start =
      tgl_registers::DdiAuxData::GetData0ForAuxControl(aux_control_).ReadFrom(mmio_buffer_);

  // Points at the next byte to be written in the data buffer.
  uint8_t* data_pointer = data_buffer.data();

  // This gets optimized to one `bswap` instruction.
  const uint8_t data_start_bytes[4] = {static_cast<uint8_t>(aux_data_start.swapped_bytes() >> 24),
                                       static_cast<uint8_t>(aux_data_start.swapped_bytes() >> 16),
                                       static_cast<uint8_t>(aux_data_start.swapped_bytes() >> 8),
                                       static_cast<uint8_t>(aux_data_start.swapped_bytes())};

  // Save the command byte so we can return it later. The compiler will optimize
  // away the extra variable.
  const uint8_t header_byte = data_start_bytes[0];

  {
    const int copy_size = std::min(data_left, 3);
    std::memcpy(data_pointer, data_start_bytes + 1, copy_size);
    data_pointer += copy_size;
    data_left -= copy_size;
  }

  // Points 4 bytes below the MMIO address of the AUX DDI buffer being read.
  zx_off_t aux_data_mmio_address = aux_data_start.reg_addr();

  while (data_left > 0) {
    aux_data_mmio_address += 4;
    const uint32_t swapped_bytes = mmio_buffer_->Read32(aux_data_mmio_address);

    if (data_left >= 4) {
      // Fast path. This gets optimized to one `bswap` instruction.
      data_pointer[0] = static_cast<uint8_t>(swapped_bytes >> 24);
      data_pointer[1] = static_cast<uint8_t>(swapped_bytes >> 16);
      data_pointer[2] = static_cast<uint8_t>(swapped_bytes >> 8);
      data_pointer[3] = static_cast<uint8_t>(swapped_bytes);

      data_left -= 4;
      data_pointer += 4;
    } else {
      // This gets optimized to one `bswap` instruction.
      const uint8_t data_bytes[4] = {
          static_cast<uint8_t>(swapped_bytes >> 24), static_cast<uint8_t>(swapped_bytes >> 16),
          static_cast<uint8_t>(swapped_bytes >> 8), static_cast<uint8_t>(swapped_bytes)};

      std::memcpy(data_pointer, data_bytes, data_left);

      // We don't need to update `data_pointer` on the slow path because we'll
      // exit the loop. Adding 4 here would yield undefined behavior, because it
      // would get the pointer past the buffer it points to.
      data_left = 0;
    }
  }

  return {.reply_header = header_byte, .reply_data_size = aux_data_size};
}

void DdiAuxChannel::FixConfig() {
  // TODO(fxbug.dev/31313): Support interrupts
  aux_control_.set_interrupt_on_done(true);

  if (aux_control_.timeout_timer_select() != tgl_registers::DdiAuxControl::kTimeoutLarge) {
    zxlogf(TRACE, "DDI AUX channel transaction timeout select was %u. Set to maximum.",
           aux_control_.timeout_timer_select());
    aux_control_.set_timeout_timer_select(tgl_registers::DdiAuxControl::kTimeoutLarge);
  }
  if (aux_control_.fast_wake_sync_pulse_count() !=
      tgl_registers::DdiAuxControl::kFastWakeSyncPulseCount) {
    zxlogf(WARNING, "DDI AUX channel fast wake pulse count was incorrectly set to %u. Fixed.",
           aux_control_.fast_wake_sync_pulse_count());
    aux_control_.set_fast_wake_sync_pulse_count(
        tgl_registers::DdiAuxControl::kFastWakeSyncPulseCount);
  }
  if (aux_control_.sync_pulse_count() < tgl_registers::DdiAuxControl::kMinSyncPulseCount) {
    zxlogf(WARNING, "DDI AUX channel wake pulse count was incorrectly set to %u. Fixed.",
           aux_control_.sync_pulse_count());
    aux_control_.set_sync_pulse_count(tgl_registers::DdiAuxControl::kMinSyncPulseCount);
  }
}

}  // namespace i915_tgl
