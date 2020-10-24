// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_NS8250_H_
#define ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_NS8250_H_

#include <zircon/boot/driver-config.h>

#include <hwreg/bitfields.h>

#include "uart.h"

// 8250 and derivatives, including 16550.

namespace uart {
namespace ns8250 {

constexpr uint16_t kPortCount = 8;

constexpr uint32_t kDefaultBaudRate = 115200;
constexpr uint32_t kMaxBaudRate = 115200;

constexpr uint8_t kFifoDepth16750 = 64;
constexpr uint8_t kFifoDepth16550A = 16;
constexpr uint8_t kFifoDepthGeneric = 1;

enum class InterruptType : uint8_t {
  kNone = 0b0001,
  kRxLineStatus = 0b0110,
  kRxDataAvailable = 0b0100,
  kCharTimeout = 0b1100,
  kTxEmpty = 0b0010,
  kModemStatus = 0b0000,
};

class RxBufferRegister : public hwreg::RegisterBase<RxBufferRegister, uint8_t> {
 public:
  DEF_FIELD(7, 0, data);
  static auto Get() { return hwreg::RegisterAddr<RxBufferRegister>(0); }
};

class TxBufferRegister : public hwreg::RegisterBase<TxBufferRegister, uint8_t> {
 public:
  DEF_FIELD(7, 0, data);
  static auto Get() { return hwreg::RegisterAddr<TxBufferRegister>(0); }
};

class InterruptEnableRegister : public hwreg::RegisterBase<InterruptEnableRegister, uint8_t> {
 public:
  DEF_RSVDZ_FIELD(7, 4);
  DEF_BIT(3, modem_status);
  DEF_BIT(2, line_status);
  DEF_BIT(1, tx_empty);
  DEF_BIT(0, rx_available);
  static auto Get() { return hwreg::RegisterAddr<InterruptEnableRegister>(1); }
};

class InterruptIdentRegister : public hwreg::RegisterBase<InterruptIdentRegister, uint8_t> {
 public:
  DEF_FIELD(7, 6, fifos_enabled);
  DEF_BIT(5, extended_fifo_enabled);
  DEF_RSVDZ_BIT(4);
  DEF_ENUM_FIELD(InterruptType, 3, 0, interrupt_id);
  static auto Get() { return hwreg::RegisterAddr<InterruptIdentRegister>(2); }
};

class FifoControlRegister : public hwreg::RegisterBase<FifoControlRegister, uint8_t> {
 public:
  DEF_FIELD(7, 6, receiver_trigger);
  DEF_BIT(5, extended_fifo_enable);
  DEF_RSVDZ_BIT(4);
  DEF_BIT(3, dma_mode);
  DEF_BIT(2, tx_fifo_reset);
  DEF_BIT(1, rx_fifo_reset);
  DEF_BIT(0, fifo_enable);

  static constexpr uint8_t kMaxTriggerLevel = 0b11;

  static auto Get() { return hwreg::RegisterAddr<FifoControlRegister>(2); }
};

class LineControlRegister : public hwreg::RegisterBase<LineControlRegister, uint8_t> {
 public:
  DEF_BIT(7, divisor_latch_access);
  DEF_BIT(6, break_control);
  DEF_BIT(5, stick_parity);
  DEF_BIT(4, even_parity);
  DEF_BIT(3, parity_enable);
  DEF_BIT(2, stop_bits);
  DEF_FIELD(1, 0, word_length);

  static constexpr uint8_t kWordLength5 = 0b00;
  static constexpr uint8_t kWordLength6 = 0b01;
  static constexpr uint8_t kWordLength7 = 0b10;
  static constexpr uint8_t kWordLength8 = 0b11;

  static constexpr uint8_t kStopBits1 = 0b0;
  static constexpr uint8_t kStopBits2 = 0b1;

  static auto Get() { return hwreg::RegisterAddr<LineControlRegister>(3); }
};

class ModemControlRegister : public hwreg::RegisterBase<ModemControlRegister, uint8_t> {
 public:
  DEF_RSVDZ_FIELD(7, 6);
  DEF_BIT(5, automatic_flow_control_enable);
  DEF_BIT(4, loop);
  DEF_BIT(3, auxiliary_out_2);
  DEF_BIT(2, auxiliary_out_1);
  DEF_BIT(1, request_to_send);
  DEF_BIT(0, data_terminal_ready);
  static auto Get() { return hwreg::RegisterAddr<ModemControlRegister>(4); }
};

class LineStatusRegister : public hwreg::RegisterBase<LineStatusRegister, uint8_t> {
 public:
  DEF_BIT(7, error_in_rx_fifo);
  DEF_BIT(6, tx_empty);
  DEF_BIT(5, tx_register_empty);
  DEF_BIT(4, break_interrupt);
  DEF_BIT(3, framing_error);
  DEF_BIT(2, parity_error);
  DEF_BIT(1, overrun_error);
  DEF_BIT(0, data_ready);
  static auto Get() { return hwreg::RegisterAddr<LineStatusRegister>(5); }
};

class ModemStatusRegister : public hwreg::RegisterBase<ModemStatusRegister, uint8_t> {
 public:
  DEF_BIT(7, data_carrier_detect);
  DEF_BIT(6, ring_indicator);
  DEF_BIT(5, data_set_ready);
  DEF_BIT(4, clear_to_send);
  DEF_BIT(3, delta_data_carrier_detect);
  DEF_BIT(2, trailing_edge_ring_indicator);
  DEF_BIT(1, delta_data_set_ready);
  DEF_BIT(0, delta_clear_to_send);
  static auto Get() { return hwreg::RegisterAddr<ModemStatusRegister>(6); }
};

class ScratchRegister : public hwreg::RegisterBase<ScratchRegister, uint8_t> {
 public:
  DEF_FIELD(7, 0, data);
  static auto Get() { return hwreg::RegisterAddr<ScratchRegister>(7); }
};

class DivisorLatchLowerRegister : public hwreg::RegisterBase<DivisorLatchLowerRegister, uint8_t> {
 public:
  DEF_FIELD(7, 0, data);
  static auto Get() { return hwreg::RegisterAddr<DivisorLatchLowerRegister>(0); }
};

class DivisorLatchUpperRegister : public hwreg::RegisterBase<DivisorLatchUpperRegister, uint8_t> {
 public:
  DEF_FIELD(7, 0, data);
  static auto Get() { return hwreg::RegisterAddr<DivisorLatchUpperRegister>(1); }
};

// This provides the actual driver logic common to MMIO and PIO variants.
template <uint32_t KdrvExtra, typename KdrvConfig>
class DriverImpl
    : public DriverBase<DriverImpl<KdrvExtra, KdrvConfig>, KdrvExtra, KdrvConfig, kPortCount> {
 public:
  template <typename... Args>
  explicit DriverImpl(Args&&... args)
      : DriverBase<DriverImpl, KdrvExtra, KdrvConfig, kPortCount>(std::forward<Args>(args)...) {}

  template <class IoProvider>
  void Init(IoProvider& io) {
    auto divisor = kMaxBaudRate / kDefaultBaudRate;

    // Get basic config done so that tx functions.

    // Disable all interrupts.
    InterruptEnableRegister::Get().FromValue(0).WriteTo(io.io());

    auto lcr = LineControlRegister::Get().FromValue(0);
    lcr.set_divisor_latch_access(true).WriteTo(io.io());
    auto dllr = DivisorLatchLowerRegister::Get().FromValue(0);
    auto dlur = DivisorLatchUpperRegister::Get().FromValue(0);
    dllr.set_data(static_cast<uint8_t>(divisor)).WriteTo(io.io());
    dlur.set_data(static_cast<uint8_t>(divisor >> 8)).WriteTo(io.io());

    auto fcr = FifoControlRegister::Get().FromValue(0);
    fcr.set_fifo_enable(true)
        .set_rx_fifo_reset(true)
        .set_tx_fifo_reset(true)
        .set_receiver_trigger(FifoControlRegister::kMaxTriggerLevel)
        // Must be done while divisor latch is enabled.
        .set_extended_fifo_enable(true)
        .WriteTo(io.io());

    lcr.set_divisor_latch_access(false)
        .set_word_length(LineControlRegister::kWordLength8)
        .set_stop_bits(LineControlRegister::kStopBits1)
        .WriteTo(io.io());

    // Drive flow control bits high since we don't actively manage them.
    auto mcr = ModemControlRegister::Get().FromValue(0);
    mcr.set_data_terminal_ready(true).set_request_to_send(true).WriteTo(io.io());

    // Figure out the FIFO depth.
    auto iir = InterruptIdentRegister::Get().ReadFrom(io.io());
    if (iir.fifos_enabled()) {
      // This is a 16750 or a 16550A.
      fifo_depth_ = iir.extended_fifo_enabled() ? kFifoDepth16750 : kFifoDepth16550A;
    } else {
      fifo_depth_ = kFifoDepthGeneric;
    }
  }

  template <class IoProvider>
  bool TxReady(IoProvider& io) {
    return LineStatusRegister::Get().ReadFrom(io.io()).tx_empty();
  }

  template <class IoProvider, typename It1, typename It2>
  auto Write(IoProvider& io, It1 it, const It2& end) {
    // The FIFO is empty now, so fill it completely.
    auto tx = TxBufferRegister::Get().FromValue(0);
    auto space = fifo_depth_;
    do {
      tx.set_data(*it).WriteTo(io.io());
    } while (++it != end && --space > 0);
    return it;
  }

  template <class IoProvider>
  std::optional<uint8_t> Read(IoProvider& io) {
    if (LineStatusRegister::Get().ReadFrom(io.io()).data_ready()) {
      return RxBufferRegister::Get().ReadFrom(io.io()).data();
    }
    return {};
  }

  template <class IoProvider>
  void EnableTxInterrupt(IoProvider& io, bool enable = true) {
    auto ier = InterruptEnableRegister::Get().FromValue(0);
    ier.set_rx_available(true).set_tx_empty(enable).WriteTo(io.io());
  }

  template <class IoProvider>
  void EnableRxInterrupt(IoProvider& io, bool enable = true) {
    auto ier = InterruptEnableRegister::Get().FromValue(0);
    ier.set_rx_available(enable).WriteTo(io.io());
  }

  template <class IoProvider>
  void InitInterrupt(IoProvider& io) {
    // Enable receive interrupts.
    EnableRxInterrupt(io);

    // Modem Control Register: Auxiliary Output 2 is another IRQ enable bit.
    auto mcr = ModemControlRegister::Get().FromValue(0);
    mcr.set_auxiliary_out_2(true).WriteTo(io.io());
  }

  template <class IoProvider, typename Tx, typename Rx>
  void Interrupt(IoProvider& io, Tx&& tx, Rx&& rx) {
    auto iir = InterruptIdentRegister::Get();
    InterruptType id;
    while ((id = iir.ReadFrom(io.io()).interrupt_id()) != InterruptType::kNone) {
      switch (id) {
        case InterruptType::kRxDataAvailable:
        case InterruptType::kCharTimeout:
          // Read the character if there's a place to put it.
          rx([&]() { return RxBufferRegister::Get().ReadFrom(io.io()).data(); },
             [&]() {
               // If the buffer is full, disable the receive interrupt instead.
               EnableRxInterrupt(io, false);
             });
          break;

        case InterruptType::kTxEmpty:
          tx();
          EnableTxInterrupt(io, false);
          break;

        case InterruptType::kRxLineStatus:
          LineStatusRegister::Get().ReadFrom(io.io());
          break;

        default:
          ZX_PANIC("unhandled interrupt ID %#x\n", id);
      }
    }
  }

 protected:
  uint8_t fifo_depth_ = kFifoDepthGeneric;
};

// uart::KernelDriver UartDriver API for PIO via MMIO.
using MmioDriver = DriverImpl<KDRV_I8250_MMIO_UART, dcfg_simple_t>;

// uart::KernelDriver UartDriver API for direct PIO.
using PioDriver = DriverImpl<KDRV_I8250_PIO_UART, dcfg_simple_pio_t>;

// Traditional COM0 configuration.
static constexpr dcfg_simple_pio_t kLegacyConfig{0x3f8, 4};

}  // namespace ns8250
}  // namespace uart

#endif  // ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_NS8250_H_
