// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_IMX_H_
#define LIB_UART_IMX_H_

#include <zircon/boot/driver-config.h>

#include <utility>

#include <hwreg/bitfields.h>

#include "uart.h"

namespace uart::imx {

// USR1
struct StatusRegister1 : public hwreg::RegisterBase<StatusRegister1, uint32_t> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_BIT(15, parityerr);
  DEF_BIT(14, rtss);
  DEF_BIT(13, trdy);
  DEF_BIT(12, rtsd);
  DEF_BIT(11, escf);
  DEF_BIT(10, framerr);
  DEF_BIT(9, rrdy);
  DEF_BIT(8, agtim);
  DEF_BIT(7, dtrd);
  DEF_BIT(6, rxds);
  DEF_BIT(5, airint);
  DEF_BIT(4, awake);
  DEF_BIT(3, sad);
  DEF_RSVDZ_FIELD(2, 0);
  static auto Get() { return hwreg::RegisterAddr<StatusRegister1>(0x94); }
};

// USR2
struct StatusRegister2 : public hwreg::RegisterBase<StatusRegister2, uint32_t> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_BIT(15, adet);
  DEF_BIT(14, txfe);
  DEF_BIT(13, dtrf);
  DEF_BIT(12, idle);
  DEF_BIT(11, acst);
  DEF_BIT(10, ridelt);
  DEF_BIT(9, riin);
  DEF_BIT(8, irint);
  DEF_BIT(7, wake);
  DEF_BIT(6, dcddelt);
  DEF_BIT(5, dcdin);
  DEF_BIT(4, rtsf);
  DEF_BIT(3, txdc);
  DEF_BIT(2, brcd);
  DEF_BIT(1, ore);
  DEF_BIT(0, rdr);
  static auto Get() { return hwreg::RegisterAddr<StatusRegister2>(0x98); }
};

// URXD
struct ReceiverRegister : public hwreg::RegisterBase<ReceiverRegister, uint32_t> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_BIT(15, charrdy);
  DEF_BIT(14, err);
  DEF_BIT(13, ovrrun);
  DEF_BIT(12, frmerr);
  DEF_BIT(11, brk);
  DEF_BIT(10, prerr);
  DEF_RSVDZ_FIELD(9, 8);
  DEF_FIELD(7, 0, rx_data);
  static auto Get() { return hwreg::RegisterAddr<ReceiverRegister>(0x0); }
};

// UTXD
struct TransmitterRegister : public hwreg::RegisterBase<TransmitterRegister, uint32_t> {
  DEF_RSVDZ_FIELD(31, 8);
  DEF_FIELD(7, 0, tx_data);
  static auto Get() { return hwreg::RegisterAddr<TransmitterRegister>(0x40); }
};

// UCR2
struct ControlRegister2 : public hwreg::RegisterBase<ControlRegister2, uint32_t> {
  DEF_RSVDZ_FIELD(31, 16);
  DEF_BIT(15, esci);
  DEF_BIT(14, irts);
  DEF_BIT(13, ctsc);
  DEF_BIT(12, cts);
  DEF_BIT(11, escen);
  DEF_FIELD(10, 9, rtec);
  DEF_BIT(8, pren);
  DEF_BIT(7, proe);
  DEF_BIT(6, stpb);
  DEF_BIT(5, ws);
  DEF_BIT(4, rtsen);
  DEF_BIT(3, aten);
  DEF_BIT(2, txen);
  DEF_BIT(1, rxen);
  DEF_BIT(0, srst);
  static auto Get() { return hwreg::RegisterAddr<ControlRegister2>(0x84); }
};

struct Driver : public DriverBase<Driver, ZBI_KERNEL_DRIVER_IMX_UART, zbi_dcfg_simple_t> {
  template <typename... Args>
  explicit Driver(Args&&... args)
      : DriverBase<Driver, ZBI_KERNEL_DRIVER_IMX_UART, zbi_dcfg_simple_t>(
            std::forward<Args>(args)...) {}

  static constexpr std::string_view config_name() { return "imx"; }

  template <class IoProvider>
  void Init(IoProvider& io) {
    ControlRegister2::Get().ReadFrom(io.io()).set_rxen(true).set_txen(true).WriteTo(io.io());
  }

  template <class IoProvider>
  uint32_t TxReady(IoProvider& io) {
    auto sr = StatusRegister1::Get().ReadFrom(io.io());
    return sr.trdy();
  }

  template <class IoProvider, typename It1, typename It2>
  auto Write(IoProvider& io, uint32_t ready_space, It1 it, const It2& end) {
    auto tx = TransmitterRegister::Get().FromValue(0);
    do {
      tx.set_tx_data(*it).WriteTo(io.io());
    } while (++it != end && --ready_space > 0);
    return it;
  }

  template <class IoProvider>
  std::optional<uint8_t> Read(IoProvider& io) {
    if (StatusRegister2::Get().ReadFrom(io.io()).rdr() == 0) {
      return {};
    }
    return ReceiverRegister::Get().ReadFrom(io.io()).rx_data();
  }

  template <class IoProvider>
  void EnableTxInterrupt(IoProvider& io, bool enable = true) {
    // Stubbed out implementation that does nothing.
    // TODO(fxbug.dev/115620): implement me
  }

  template <class IoProvider>
  void EnableRxInterrupt(IoProvider& io, bool enable = true) {
    // Stubbed out implementation that does nothing.
    // TODO(fxbug.dev/115620): implement me
  }
};

}  // namespace uart::imx

#endif  // LIB_UART_IMX_H_
