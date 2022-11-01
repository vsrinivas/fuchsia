// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_NS8250_H_
#define LIB_UART_NS8250_H_

#include <lib/acpi_lite/debug_port.h>
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

// Traditional COM1 configuration.
constexpr zbi_dcfg_simple_pio_t kLegacyConfig{.base = 0x3f8, .irq = 4};

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
  using Base = DriverBase<DriverImpl<KdrvExtra, KdrvConfig>, KdrvExtra, KdrvConfig, kPortCount>;

  static constexpr std::string_view config_name() {
    if constexpr (KdrvExtra == ZBI_KERNEL_DRIVER_I8250_PIO_UART) {
      return "ioport";
    }
#if defined(__i386__) || defined(__x86_64__)
    if constexpr (KdrvExtra == ZBI_KERNEL_DRIVER_I8250_MMIO_UART) {
      return "mmio";
    }
#endif
    return "ns8250";
  }

  template <typename... Args>
  explicit DriverImpl(Args&&... args) : Base(std::forward<Args>(args)...) {}

  using Base::MaybeCreate;

  static std::optional<DriverImpl> MaybeCreate(
      const acpi_lite::AcpiDebugPortDescriptor& debug_port) {
    if constexpr (KdrvExtra == ZBI_KERNEL_DRIVER_I8250_MMIO_UART) {
      if (debug_port.type == acpi_lite::AcpiDebugPortDescriptor::Type::kMmio) {
        return DriverImpl(KdrvConfig{.mmio_phys = debug_port.address});
      }
    }

    if constexpr (KdrvExtra == ZBI_KERNEL_DRIVER_I8250_PIO_UART) {
      if (debug_port.type == acpi_lite::AcpiDebugPortDescriptor::Type::kPio) {
        return DriverImpl(KdrvConfig{.base = static_cast<uint16_t>(debug_port.address)});
      }
    }
    return {};
  }

  static std::optional<DriverImpl> MaybeCreate(std::string_view string) {
    if constexpr (KdrvExtra == ZBI_KERNEL_DRIVER_I8250_PIO_UART) {
      if (string == "legacy") {
        return DriverImpl{kLegacyConfig};
      }
    }
    return Base::MaybeCreate(string);
  }

  template <class IoProvider>
  void Init(IoProvider& io) {
    // Get basic config done so that tx functions.

    // Disable all interrupts.
    InterruptEnableRegister::Get().FromValue(0).WriteTo(io.io());

    // Extended FIFO mode must be enabled while the divisor latch is.
    // Be sure to preserve the line controls, modulo divisor latch access,
    // which should be disabled immediately after configuring the FIFO.
    auto lcr = LineControlRegister::Get().ReadFrom(io.io());
    lcr.set_divisor_latch_access(true).WriteTo(io.io());

    auto fcr = FifoControlRegister::Get().FromValue(0);
    fcr.set_fifo_enable(true)
        .set_rx_fifo_reset(true)
        .set_tx_fifo_reset(true)
        .set_receiver_trigger(FifoControlRegister::kMaxTriggerLevel)
        .set_extended_fifo_enable(true)
        .WriteTo(io.io());

    // Commit divisor by clearing the latch.
    lcr.set_divisor_latch_access(false).WriteTo(io.io());

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
  void SetLineControl(IoProvider& io, std::optional<DataBits> data_bits,
                      std::optional<Parity> parity, std::optional<StopBits> stop_bits) {
    constexpr uint32_t kDivisor = kMaxBaudRate / kDefaultBaudRate;

    LineControlRegister::Get().FromValue(0).set_divisor_latch_access(true).WriteTo(io.io());

    DivisorLatchLowerRegister::Get()
        .FromValue(0)
        .set_data(static_cast<uint8_t>(kDivisor))
        .WriteTo(io.io());
    DivisorLatchUpperRegister::Get()
        .FromValue(0)
        .set_data(static_cast<uint8_t>(kDivisor >> 8))
        .WriteTo(io.io());

    auto lcr = LineControlRegister::Get().FromValue(0).set_divisor_latch_access(false);

    if (data_bits) {
      uint8_t word_length = [bits = *data_bits]() {
        switch (bits) {
          case DataBits::k5:
            return LineControlRegister::kWordLength5;
          case DataBits::k6:
            return LineControlRegister::kWordLength6;
          case DataBits::k7:
            return LineControlRegister::kWordLength7;
          case DataBits::k8:
            return LineControlRegister::kWordLength8;
        }
      }();
      lcr.set_word_length(word_length);
    }

    if (parity) {
      lcr.set_parity_enable(*parity != Parity::kNone).set_even_parity(*parity == Parity::kEven);
    }

    if (stop_bits) {
      uint8_t num_stop_bits = [bits = *stop_bits]() {
        switch (bits) {
          case StopBits::k1:
            return LineControlRegister::kStopBits1;
          case StopBits::k2:
            return LineControlRegister::kStopBits2;
        }
      }();
      lcr.set_stop_bits(num_stop_bits);
    }

    lcr.WriteTo(io.io());
  }

  template <class IoProvider>
  bool TxReady(IoProvider& io) {
    return LineStatusRegister::Get().ReadFrom(io.io()).tx_empty();
  }

  template <class IoProvider, typename It1, typename It2>
  auto Write(IoProvider& io, bool, It1 it, const It2& end) {
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
          ZX_PANIC("unhandled interrupt ID %#x", id);
      }
    }
  }

 protected:
  uint8_t fifo_depth_ = kFifoDepthGeneric;
};

// uart::KernelDriver UartDriver API for PIO via MMIO.
using MmioDriver = DriverImpl<ZBI_KERNEL_DRIVER_I8250_MMIO_UART, zbi_dcfg_simple_t>;

// uart::KernelDriver UartDriver API for direct PIO.
using PioDriver = DriverImpl<ZBI_KERNEL_DRIVER_I8250_PIO_UART, zbi_dcfg_simple_pio_t>;

// uart::KernelDriver UartDriver API for PIO via MMIO using legacy item type.
using LegacyDw8250Driver = DriverImpl<ZBI_KERNEL_DRIVER_DW8250_UART, zbi_dcfg_simple_t>;

}  // namespace ns8250
}  // namespace uart

#endif  // LIB_UART_NS8250_H_
