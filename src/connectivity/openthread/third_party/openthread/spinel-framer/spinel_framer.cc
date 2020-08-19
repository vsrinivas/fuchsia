/*
 *    Copyright (c) 2017, The OpenThread Authors.
 *    All rights reserved.
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following conditions are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *    3. Neither the name of the copyright holder nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 *    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spinel_framer.h"

#include <string.h>

#include <ddk/debug.h>

namespace ot {

const int kImmediateRetryCnt = 5;
const int kFastRetryCnt = 15;
const uint32_t kImmediateRetryTimeoutMSec = 1;
const uint32_t kFastRetryTimeoutMSec = 10;
const uint32_t kSlowRetryTimeoutMSec = 33;

/* ------------------------------------------------------------------------- */
/* MARK: Statistics */

void SpinelFramer::ClearStats(void) {
  dump_stats_ = true;
  slave_reset_count_ = 0;
  spi_frame_count_ = 0;
  spi_valid_frame_count_ = 0;
  spi_garbage_frame_count_ = 0;
  spi_duplex_frame_count_ = 0;
  spi_unresponsive_frame_count_ = 0;
  rx_frame_byte_count_ = 0;
  tx_frame_byte_count_ = 0;
  rx_frame_count_ = 0;
  tx_frame_count_ = 0;
}

void SpinelFramer::LogDebugBuffer(const char *desc, const uint8_t *buffer_ptr,
                                  uint16_t buffer_len) {
  int i = 0;

  while (i < buffer_len) {
    int j;
    char dump_string[kDebugBytesPerLine * 3 + 1];

    for (j = 0; i < buffer_len && j < kDebugBytesPerLine; i++, j++) {
      snprintf(dump_string + j * 3, kDebugBytesPerLine * 3 + 1, "%02X ", buffer_ptr[i]);
    }

    zxlogf(DEBUG, "%s: %s", desc, dump_string);
  }
}

/* ------------------------------------------------------------------------- */
/* MARK: SPI Transfer Functions */

static void spi_header_set_flag_byte(uint8_t *header, uint8_t value) { header[0] = value; }

static void spi_header_set_accept_len(uint8_t *header, uint16_t len) {
  header[1] = ((len >> 0) & 0xFF);
  header[2] = ((len >> 8) & 0xFF);
}

static void spi_header_set_data_len(uint8_t *header, uint16_t len) {
  header[3] = ((len >> 0) & 0xFF);
  header[4] = ((len >> 8) & 0xFF);
}

static uint8_t spi_header_get_flag_byte(const uint8_t *header) { return header[0]; }

static uint16_t spi_header_get_accept_len(const uint8_t *header) {
  return (header[1] + static_cast<uint16_t>(header[2] << 8));
}

static uint16_t spi_header_get_data_len(const uint8_t *header) {
  return (header[3] + static_cast<uint16_t>(header[4] << 8));
}

uint8_t *SpinelFramer::GetRealRxFrameStart(void) {
  uint8_t *ret = spi_rx_frame_buffer_;
  int i = 0;

  for (i = 0; i < spi_rx_align_allowance_; i++) {
    if (ret[0] != 0xFF) {
      break;
    }
    ret++;
  }

  return ret;
}

zx_status_t SpinelFramer::DoSpiXfer(uint16_t len) {
  zx_status_t status = ZX_OK;
  size_t rx_actual = 0;
  uint16_t tot_len = len + kHeaderLen + spi_rx_align_allowance_;

  status = spi_.Exchange(spi_tx_frame_buffer_, tot_len, spi_rx_frame_buffer_, tot_len, &rx_actual);

  if (status == ZX_OK) {
    LogDebugBuffer("SPI-TX", spi_tx_frame_buffer_, tot_len);
    LogDebugBuffer("SPI-RX", spi_rx_frame_buffer_, tot_len);

    spi_frame_count_++;
  }
  return status;
}

void SpinelFramer::DebugSpiHeader(const char *hint) {
  const uint8_t *spiRxFrameBuffer = GetRealRxFrameStart();

  zxlogf(DEBUG, "%s-TX: H:%02X ACCEPT:%d DATA:%0d", hint,
         spi_header_get_flag_byte(spi_tx_frame_buffer_),
         spi_header_get_accept_len(spi_tx_frame_buffer_),
         spi_header_get_data_len(spi_tx_frame_buffer_));

  zxlogf(DEBUG, "%s-RX: H:%02X ACCEPT:%d DATA:%0d", hint,
         spi_header_get_flag_byte(spiRxFrameBuffer), spi_header_get_accept_len(spiRxFrameBuffer),
         spi_header_get_data_len(spiRxFrameBuffer));
}

zx_status_t SpinelFramer::PushPullSpi(void) {
  zx_status_t status = ZX_OK;
  uint16_t spi_xfer_bytes = 0;
  const uint8_t *spiRxFrameBuffer = NULL;
  uint8_t slave_header;
  uint16_t slave_max_rx;
  uint8_t successful_exchanges = 0;
  static uint16_t slave_data_len;

  // For now, spi_rx_payload_size_ must be zero
  // when entering this function. This may change
  // at some point, for now this makes things
  // much easier.
  assert(spi_rx_payload_size_ == 0);

  if (spi_valid_frame_count_ == 0) {
    // Set the reset flag to indicate to our slave that we
    // are coming up from scratch.
    spi_header_set_flag_byte(spi_tx_frame_buffer_, kHeaderResetFlag | kHeaderPatternValue);
  } else {
    spi_header_set_flag_byte(spi_tx_frame_buffer_, kHeaderPatternValue);
  }

  // Zero out our rx_accept and our data_len for now.
  spi_header_set_accept_len(spi_tx_frame_buffer_, 0);
  spi_header_set_data_len(spi_tx_frame_buffer_, 0);

  // Sanity check.
  if (slave_data_len > kMaxFrameSize) {
    slave_data_len = 0;
  }

  if (spi_tx_is_ready_) {
    // Go ahead and try to immediately send a frame if we have it queued up.
    spi_header_set_data_len(spi_tx_frame_buffer_, spi_tx_payload_size_);

    if (spi_tx_payload_size_ > spi_xfer_bytes) {
      spi_xfer_bytes = spi_tx_payload_size_;
    }
  }

  if (spi_rx_payload_size_ == 0) {
    if (slave_data_len != 0) {
      // In a previous transaction the slave indicated
      // it had something to send us. Make sure our
      // transaction is large enough to handle it.
      if (slave_data_len > spi_xfer_bytes) {
        spi_xfer_bytes = slave_data_len;
      }
    } else {
      // Set up a minimum transfer size to allow small
      // frames the slave wants to send us to be handled
      // in a single transaction.
      if (spi_small_packet_size_ > spi_xfer_bytes) {
        spi_xfer_bytes = static_cast<uint16_t>(spi_small_packet_size_);
      }
    }

    spi_header_set_accept_len(spi_tx_frame_buffer_, spi_xfer_bytes);
  }

  // Perform the SPI transaction.
  status = DoSpiXfer(spi_xfer_bytes);

  if (status != ZX_OK) {
    perror("PushPullSpi:DoSpiXfer");
    zxlogf(ERROR, "PushPullSpi:DoSpiXfer");

    goto bail;
  }

  // Account for misalignment (0xFF bytes at the start)
  spiRxFrameBuffer = GetRealRxFrameStart();

  DebugSpiHeader("push_pull");

  slave_header = spi_header_get_flag_byte(spiRxFrameBuffer);

  if ((slave_header == 0xFF) || (slave_header == 0x00)) {
    if ((slave_header == spiRxFrameBuffer[1]) && (slave_header == spiRxFrameBuffer[2]) &&
        (slave_header == spiRxFrameBuffer[3]) && (slave_header == spiRxFrameBuffer[4])) {
      // Device is off or in a bad state.
      // In some cases may be induced by flow control.
      zxlogf(DEBUG, "Slave did not respond to frame. (Header was all 0x%02X)", slave_header);
      spi_unresponsive_frame_count_++;
    } else {
      // Header is full of garbage
      zxlogf(DEBUG, "1.Garbage in header : %02X %02X %02X %02X %02X", spiRxFrameBuffer[0],
             spiRxFrameBuffer[1], spiRxFrameBuffer[2], spiRxFrameBuffer[3], spiRxFrameBuffer[4]);
      spi_garbage_frame_count_++;
      LogDebugBuffer("SPI-TX", spi_tx_frame_buffer_,
                     spi_xfer_bytes + kHeaderLen + spi_rx_align_allowance_);
      LogDebugBuffer("SPI-RX", spi_rx_frame_buffer_,
                     spi_xfer_bytes + kHeaderLen + spi_rx_align_allowance_);
    }
    spi_tx_refused_count_++;
    goto bail;
  }

  slave_max_rx = spi_header_get_accept_len(spiRxFrameBuffer);
  slave_data_len = spi_header_get_data_len(spiRxFrameBuffer);

  if (((slave_header & kHeaderPatternMask) != kHeaderPatternValue) ||
      (slave_max_rx > kMaxFrameSize) || (slave_data_len > kMaxFrameSize)) {
    spi_garbage_frame_count_++;
    spi_tx_refused_count_++;
    slave_data_len = 0;
    zxlogf(DEBUG, "2.Garbage in header : %02X %02X %02X %02X %02X", spiRxFrameBuffer[0],
           spiRxFrameBuffer[1], spiRxFrameBuffer[2], spiRxFrameBuffer[3], spiRxFrameBuffer[4]);
    LogDebugBuffer("SPI-TX", spi_tx_frame_buffer_,
                   spi_xfer_bytes + kHeaderLen + spi_rx_align_allowance_);
    LogDebugBuffer("SPI-RX", spi_rx_frame_buffer_,
                   spi_xfer_bytes + kHeaderLen + spi_rx_align_allowance_);
    goto bail;
  }

  spi_valid_frame_count_++;

  if ((slave_header & kHeaderResetFlag) == kHeaderResetFlag) {
    slave_reset_count_++;
    zxlogf(INFO, "Slave did reset (%llu resets so far)", (unsigned long long)slave_reset_count_);
    slave_did_reset_ = true;
    dump_stats_ = true;
  }

  // Handle received packet, if any.
  if ((spi_rx_payload_size_ == 0) && (slave_data_len != 0)) {
    if (slave_data_len <= spi_header_get_accept_len(spi_tx_frame_buffer_)) {
      // We have received a packet. Set spi_rx_payload_size_ so that
      // the packet will eventually get queued up.
      spi_rx_payload_size_ = slave_data_len;

      slave_data_len = 0;

      successful_exchanges++;
      radio_data_rx_pending_ = false;
    } else {
      radio_data_rx_pending_ = true;
    }
  }

  // Handle transmitted packet, if any.
  if (spi_tx_is_ready_ && (spi_tx_payload_size_ == spi_header_get_data_len(spi_tx_frame_buffer_))) {
    if (spi_header_get_data_len(spi_tx_frame_buffer_) <= slave_max_rx) {
      // Our outbound packet has been successfully transmitted. Clear
      // spi_tx_payload_size_ and spi_tx_is_ready_ so that we can
      // pull another packet for us to send.
      spi_tx_is_ready_ = false;
      spi_tx_payload_size_ = 0;
      spi_tx_refused_count_ = 0;
      successful_exchanges++;
    } else {
      // The slave wasn't ready for what we had to
      // send them. Incrementing this counter will
      // turn on rate limiting so that we
      // don't waste a ton of CPU bombarding them
      // with useless SPI transfers.
      spi_tx_refused_count_++;
    }
  }

  if (!spi_tx_is_ready_) {
    spi_tx_refused_count_ = 0;
  }

  if (successful_exchanges == 2) {
    spi_duplex_frame_count_++;
  }
bail:
  return status;
}

bool SpinelFramer::CheckAndClearInterrupt(void) {
  bool ret = false;
  if (interrupt_fired_) {
    ret = true;
    interrupt_fired_ = false;
  }

  return ret;
}

/* ------------------------------------------------------------------------- */
/* MARK: Public functions */

void SpinelFramer::Init(ddk::SpiProtocolClient spi, uint16_t spi_rx_align_allowance) {
  spi_ = spi;
  spi_rx_align_allowance_ = spi_rx_align_allowance;  // Optional. Set to 0 by default.
  ClearStats();
}

void SpinelFramer::SetInboundAllowanceStatus(bool status) { has_inbound_allowance_ = status; }

uint32_t SpinelFramer::GetTimeoutMs(void) {
  int timeout_ms = 1000 * 60 * 60 * 24;  // 24 hours

  if (spi_tx_is_ready_ || dump_stats_) {
    // We have data to send to the radio.
    timeout_ms = 0;
  }

  if (has_inbound_allowance_ && radio_data_rx_pending_) {
    // We have more data to read from the radio
    timeout_ms = 5;
  }

  if (spi_tx_refused_count_) {
    int min_timeout = 0;

    // Spinel framer is rate-limited by the Ncp. This is
    // fairly normal behavior. Based on number of times
    // Ncp has refused a transmission, we apply a
    // minimum timeout.

    if (spi_tx_refused_count_ < kImmediateRetryCnt) {
      min_timeout = kImmediateRetryTimeoutMSec;
    } else if (spi_tx_refused_count_ < kFastRetryCnt) {
      min_timeout = kFastRetryTimeoutMSec;
    } else {
      min_timeout = kSlowRetryTimeoutMSec;
    }

    if (timeout_ms < min_timeout) {
      timeout_ms = min_timeout;
    }

    if (spi_tx_is_ready_ && !did_print_rate_limit_log_ && (spi_tx_refused_count_ > 1)) {
      zxlogf(INFO, "Ncp is rate limiting transactions");
      // Print it only once
      did_print_rate_limit_log_ = true;
    }

    if (spi_tx_refused_count_ == 30) {
      zxlogf(WARNING, "Ncp seems stuck.");
    }

    if (spi_tx_refused_count_ == 100) {
      zxlogf(WARNING, "Ncp seems REALLY stuck.");
    }
  } else {
    did_print_rate_limit_log_ = false;
  }

  return timeout_ms;
}

void SpinelFramer::HandleInterrupt() {
  interrupt_fired_ = true;
  TrySpiTransaction();
}

bool SpinelFramer::IsPacketPresent(void) {
  if (spi_rx_payload_size_ != 0) {
    // We have data that we are waiting to send out
    // so we need to wait for that to clear out
    // before we can do anything else.
    return true;
  }
  return false;
}

void SpinelFramer::SendPacketToRadio(uint8_t *packet, uint16_t length) {
  if (!spi_tx_is_ready_) {
    memcpy(&spi_tx_frame_buffer_[kHeaderLen], packet, length);
    spi_tx_payload_size_ = length;
    spi_tx_is_ready_ = true;

    // Increment counters for statistics
    rx_frame_count_++;
    rx_frame_byte_count_ += spi_tx_payload_size_;

    TrySpiTransaction();
  }
}

void SpinelFramer::ReceivePacketFromRadio(uint8_t *rxPacket, uint16_t *length) {
  const uint8_t *spiRxFrameBuffer = GetRealRxFrameStart();
  static uint8_t raw_frame_buffer[kMaxFrameSize];
  static uint16_t raw_frame_len;
  static uint16_t raw_frame_sent;

  if (raw_frame_len == 0) {
    if (slave_did_reset_) {
      // Indicates an MCU reset.
      // We don't have anything to do here because
      // raw mode doesn't have any way to signal
      // resets out-of-band.
      slave_did_reset_ = false;
    }
    if (spi_rx_payload_size_ > 0) {
      // Read the frame into raw_frame_buffer
      assert(spi_rx_payload_size_ <= sizeof(raw_frame_buffer));
      memcpy(raw_frame_buffer, &spiRxFrameBuffer[kHeaderLen], spi_rx_payload_size_);
      raw_frame_len = spi_rx_payload_size_;
      raw_frame_sent = 0;
      spi_rx_payload_size_ = 0;
    } else {
      // Nothing to do.
      goto bail;
    }
  }

  *length = raw_frame_len - raw_frame_sent;
  memcpy(rxPacket, (raw_frame_buffer + raw_frame_sent), *length);

  raw_frame_sent += *length;

  // Reset state once we have sent the entire frame.
  if (raw_frame_len == raw_frame_sent) {
    // Increment counter for statistics
    tx_frame_count_++;
    tx_frame_byte_count_ += raw_frame_len;

    raw_frame_len = raw_frame_sent = 0;
  }

bail:
  return;
}

void SpinelFramer::TrySpiTransaction() {
  if (radio_data_rx_pending_ ||
      ((spi_rx_payload_size_ == 0) && (spi_tx_is_ready_ || CheckAndClearInterrupt()))) {
    // We guard this with the above check because we don't
    // want to overwrite any previously received (but not
    // yet pushed out) frames.
    if (PushPullSpi() != ZX_OK) {
      zxlogf(ERROR, "SPI transaction failed");
    }
  }

  if (dump_stats_) {
    dump_stats_ = false;
    zxlogf(DEBUG, "STATS: slave_reset_count_=%llu", (unsigned long long)slave_reset_count_);
    zxlogf(DEBUG, "STATS: spi_frame_count_=%llu", (unsigned long long)spi_frame_count_);
    zxlogf(DEBUG, "STATS: spi_valid_frame_count_=%llu", (unsigned long long)spi_valid_frame_count_);
    zxlogf(DEBUG, "STATS: spi_duplex_frame_count_=%llu",
           (unsigned long long)spi_duplex_frame_count_);
    zxlogf(DEBUG, "STATS: spi_unresponsive_frame_count_=%llu",
           (unsigned long long)spi_unresponsive_frame_count_);
    zxlogf(DEBUG, "STATS: spi_garbage_frame_count_=%llu",
           (unsigned long long)spi_garbage_frame_count_);
    zxlogf(DEBUG, "STATS: tx_frame_count_=%llu", (unsigned long long)tx_frame_count_);
    zxlogf(DEBUG, "STATS: tx_frame_byte_count_=%llu", (unsigned long long)tx_frame_byte_count_);
    zxlogf(DEBUG, "STATS: rx_frame_count_=%llu", (unsigned long long)rx_frame_count_);
    zxlogf(DEBUG, "STATS: rx_frame_byte_count_=%llu", (unsigned long long)rx_frame_byte_count_);
  }
}

}  // namespace ot
