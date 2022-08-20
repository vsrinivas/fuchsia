// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_MOTMOT_H_
#define LIB_UART_MOTMOT_H_

#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include <hwreg/bitfields.h>

#include "uart.h"

namespace uart::motmot {

// line control register
struct ULCON : public hwreg::RegisterBase<ULCON, uint32_t> {
  // 31:6 Reserved.
  DEF_FIELD(5, 3, parity_mode);
  DEF_BIT(2, num_stop_bits);
  DEF_FIELD(1, 0, word_length);

  static auto Get() { return hwreg::RegisterAddr<ULCON>(0); }
};

// general control register
struct UCON : public hwreg::RegisterBase<UCON, uint32_t> {
  // 31:23 Reserved.
  DEF_FIELD(22, 20, tx_dma_burst_size);
  // 19 Reserved.
  DEF_FIELD(18, 16, rx_dma_burst_size);
  DEF_FIELD(15, 12, rx_timeout_interrupt_interval);
  DEF_BIT(11, rx_timeout_with_empty_rx_fifo);
  DEF_BIT(10, rx_timeout_dma_suspend_enable);
  // 9:8 Reserved.
  DEF_BIT(7, rx_timeout_enable);
  DEF_BIT(6, rx_error_status_interrupt_enable);
  DEF_BIT(5, loop_back_mode);
  DEF_BIT(4, send_break_signal);
  DEF_FIELD(3, 2, transmit_mode);
  DEF_FIELD(1, 0, receive_mode);

  static auto Get() { return hwreg::RegisterAddr<UCON>(0x4); }
};

// fifo control register
struct UFCON : public hwreg::RegisterBase<UFCON, uint32_t> {
  // 31:11 Reserved.
  DEF_FIELD(10, 8, tx_fifo_trigger_level);
  // 7 Reserved.
  DEF_FIELD(6, 4, rx_fifo_trigger_level);
  // 3 Reserved.
  DEF_BIT(2, tx_fifo_reset);
  DEF_BIT(1, rx_fifo_reset);
  DEF_BIT(0, fifo_enable);

  static auto Get() { return hwreg::RegisterAddr<UFCON>(0x8); }
};

// fifo status register
struct UFSTAT : public hwreg::RegisterBase<UFSTAT, uint32_t> {
  // 31:25 Reserved.
  DEF_BIT(24, tx_fifo_full);
  DEF_FIELD(23, 16, tx_fifo_count);
  // 15:10 Reserved.
  DEF_BIT(9, rx_fifo_error);
  DEF_BIT(8, rx_fifo_full);
  DEF_FIELD(7, 0, rx_fifo_count);

  static auto Get() { return hwreg::RegisterAddr<UFSTAT>(0x18); }
};

// transmit register
struct UTXH : public hwreg::RegisterBase<UTXH, uint32_t> {
  // 31:8 Reserved.
  DEF_FIELD(7, 0, data);

  static auto Get() { return hwreg::RegisterAddr<UTXH>(0x20); }
};

// receive register
struct URXH : public hwreg::RegisterBase<URXH, uint32_t> {
  // 31:8 Reserved.
  DEF_FIELD(7, 0, data);

  static auto Get() { return hwreg::RegisterAddr<URXH>(0x24); }
};

// interrupt mask register
struct UINTM : public hwreg::RegisterBase<UINTM, uint32_t> {
  // 31:4 Reserved.
  DEF_BIT(3, mask_cts_irq);
  DEF_BIT(2, mask_transmit_irq);
  DEF_BIT(1, mask_error_irq);
  DEF_BIT(0, mask_receive_irq);

  static auto Get() { return hwreg::RegisterAddr<UINTM>(0x38); }
};

// Universal Serial Config register
struct USI_CONFIG : public hwreg::RegisterBase<USI_CONFIG, uint32_t> {
  // 31:3 Reserved.
  DEF_BIT(2, config_i2c);
  DEF_BIT(1, config_spi);
  DEF_BIT(0, config_uart);

  static auto Get() { return hwreg::RegisterAddr<USI_CONFIG>(0xc0); }
};

// Universal Serial Control register
struct USI_CON : public hwreg::RegisterBase<USI_CON, uint32_t> {
  // 31:1 Reserved.
  DEF_BIT(0, reset);

  static auto Get() { return hwreg::RegisterAddr<USI_CON>(0xc4); }
};

// Universal Serial FIFO Depth register
struct FIFO_DEPTH : public hwreg::RegisterBase<FIFO_DEPTH, uint32_t> {
  // 31:25 Reserved.
  DEF_FIELD(24, 16, tx_fifo_depth);
  // 15:9 Reserved.
  DEF_FIELD(8, 0, rx_fifo_depth);

  static auto Get() { return hwreg::RegisterAddr<FIFO_DEPTH>(0xdc); }
};

struct Driver : public DriverBase<Driver, ZBI_KERNEL_DRIVER_MOTMOT_UART, zbi_dcfg_simple_t> {
  using Base = DriverBase<Driver, ZBI_KERNEL_DRIVER_MOTMOT_UART, zbi_dcfg_simple_t>;

  static constexpr std::string_view config_name() { return "motmot"; }

  template <typename... Args>
  explicit Driver(Args&&... args) : Base(std::forward<Args>(args)...) {}

  static std::optional<Driver> MaybeCreate(const zbi_header_t& header, const void* payload) {
    return Base::MaybeCreate(header, payload);
  }

  static std::optional<Driver> MaybeCreate(std::string_view string) {
    return Base::MaybeCreate(string);
  }

  template <class IoProvider>
  void Init(IoProvider& io) {
    // Do a very basic setup to ensure the RX and TX path is enabled and interrupts
    // are masked.

    // Read the fifo depth
    auto fifo_depth = FIFO_DEPTH::Get().ReadFrom(io.io());
    rx_fifo_depth_ = fifo_depth.rx_fifo_depth();
    tx_fifo_depth_ = fifo_depth.tx_fifo_depth();

    // Mask all IRQs
    UINTM::Get()
        .FromValue(0)
        .set_mask_cts_irq(1)
        .set_mask_transmit_irq(1)
        .set_mask_error_irq(1)
        .set_mask_receive_irq(1)
        .WriteTo(io.io());

    // Disable fifo
    UFCON::Get()
        .FromValue(0)
        .set_tx_fifo_trigger_level(0)
        .set_rx_fifo_trigger_level(0)
        .set_fifo_enable(0)
        .WriteTo(io.io());

    // Reset the fifo and wait for it to clear
    UFCON::Get().ReadFrom(io.io()).set_tx_fifo_reset(1).set_rx_fifo_reset(1).WriteTo(io.io());

    // wait for both the tx and rx fifo reset to clear
    auto ufcon_reg = UFCON::Get().FromValue(0);
    do {
      ufcon_reg.ReadFrom(io.io());
    } while (ufcon_reg.tx_fifo_reset() || ufcon_reg.rx_fifo_reset());

    // enable the fifos
    ufcon_reg.ReadFrom(io.io()).set_fifo_enable(1).WriteTo(io.io());

    // enable rx/tx
    UCON::Get()
        .ReadFrom(io.io())
        .set_transmit_mode(1)  // interrupt/polling mode
        .set_receive_mode(1)   // interrupt/polling mode
        .WriteTo(io.io());
  }

  template <class IoProvider>
  bool TxReady(IoProvider& io) {
    return !UFSTAT::Get().ReadFrom(io.io()).tx_fifo_full();
  }

  template <class IoProvider, typename It1, typename It2>
  auto Write(IoProvider& io, bool, It1 it, const It2& end) {
    UTXH::Get().FromValue(0).set_data(*it).WriteTo(io.io());
    return ++it;
  }

  template <class IoProvider>
  std::optional<uint8_t> Read(IoProvider& io) {
    if (UFSTAT::Get().ReadFrom(io.io()).rx_fifo_count() == 0) {
      return {};
    }
    // TODO(fxbug.dev/86569) handle clearing a rx fifo error
    return URXH::Get().ReadFrom(io.io()).data();
  }

  template <class IoProvider>
  void EnableTxInterrupt(IoProvider& io, bool enable = true) {
    // Stubbed out implementation that does nothing.
  }

  template <class IoProvider>
  void EnableRxInterrupt(IoProvider& io, bool enable = true) {
    // Stubbed out implementation that does nothing.
  }

 private:
  uint32_t rx_fifo_depth_ = 0;
  uint32_t tx_fifo_depth_ = 0;
};

}  // namespace uart::motmot

#endif  // LIB_UART_MOTMOT_H_
