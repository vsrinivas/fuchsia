// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "uart16550.h"

#include <fuchsia/hardware/serial/c/fidl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/serial.h>
#include <ddk/protocol/serialimpl.h>
#include <hw/inout.h>

namespace {

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
  DEF_FIELD(3, 0, interrupt_id);
  static auto Get() { return hwreg::RegisterAddr<InterruptIdentRegister>(2); }
};

class FifoControlRegister : public hwreg::RegisterBase<FifoControlRegister, uint8_t> {
 public:
  DEF_FIELD(7, 6, reciever_trigger);
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

}  // namespace

namespace uart16550 {

static constexpr int64_t kPioIndex = 0;
static constexpr int64_t kIrqIndex = 0;

static constexpr uint32_t kMaxBaudRate = 115200;
static constexpr uint8_t kDefaultConfig =
    SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 | SERIAL_PARITY_NONE;

static constexpr uint32_t kPortCount = 8;

static constexpr uint32_t kFifoDepth16750 = 64;
static constexpr uint32_t kFifoDepth16550A = 16;
static constexpr uint32_t kFifoDepthGeneric = 1;

static constexpr serial_port_info_t kInfo = {
    .serial_class = fuchsia_hardware_serial_Class_GENERIC,
    .serial_vid = 0,
    .serial_pid = 0,
};

Uart16550::Uart16550() : DeviceType(nullptr) {}

Uart16550::Uart16550(zx_device_t* parent) : DeviceType(parent), acpi_(parent) {}

zx_status_t Uart16550::Create(void* /*ctx*/, zx_device_t* parent) {
  auto dev = std::make_unique<Uart16550>(parent);

  auto status = dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Init failed\n", __func__);
    return status;
  }

  dev->DdkAdd("uart16550");

  // Release because devmgr is now in charge of the device.
  static_cast<void>(dev.release());
  return ZX_OK;
}

size_t Uart16550::FifoDepth() const { return uart_fifo_len_; }

bool Uart16550::Enabled() {
  std::lock_guard<std::mutex> lock(device_mutex_);
  return enabled_;
}

bool Uart16550::NotifyCallbackSet() {
  std::lock_guard<std::mutex> lock(device_mutex_);
  return notify_cb_.callback != nullptr;
}

// Create RX and TX FIFOs, obtain interrupt and port handles from the ACPI
// device, obtain port permissions, set up default configuration, and start the
// interrupt handler thread.
zx_status_t Uart16550::Init() {
  zx::resource io_port;
  auto status = acpi_.GetPio(kPioIndex, &io_port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: acpi_.GetPio failed\n", __func__);
    return status;
  }

  status = acpi_.MapInterrupt(kIrqIndex, &interrupt_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: acpi_.MapInterrupt failed\n", __func__);
    return status;
  }

  zx_info_resource_t resource_info;
  status =
      io_port.get_info(ZX_INFO_RESOURCE, &resource_info, sizeof(resource_info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: io_port.get_info failed\n", __func__);
    return status;
  }

  const auto port_base = static_cast<uint16_t>(resource_info.base);
  const auto port_size = static_cast<uint32_t>(resource_info.size);

  if (port_base != resource_info.base) {
    zxlogf(ERROR, "%s: overflowing UART port base\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

  if (port_size != resource_info.size) {
    zxlogf(ERROR, "%s: overflowing UART port size\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

  if (port_size != kPortCount) {
    zxlogf(ERROR, "%s: unsupported UART port count\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = zx_ioports_request(io_port.get(), port_base, port_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_ioports_request failed\n", __func__);
    return status;
  }

  {
    std::lock_guard<std::mutex> lock(device_mutex_);
    auto port_read = [](uint16_t port) -> uint8_t { return inp(port); };

    auto port_write = [](uint8_t data, uint16_t port) { outp(port, data); };

    port_io_ = PortIo(port_read, port_write, port_base);

    InitFifosLocked();
  }

  status = SerialImplConfig(kMaxBaudRate, kDefaultConfig);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SerialImplConfig failed\n", __func__);
    return status;
  }

  interrupt_thread_ = std::thread([&] { HandleInterrupts(); });

  return ZX_OK;
}

zx_status_t Uart16550::Init(zx::interrupt interrupt, fbl::Function<uint8_t(uint16_t)> port_read,
                            fbl::Function<void(uint8_t, uint16_t)> port_write) {
  interrupt_ = std::move(interrupt);
  {
    std::lock_guard<std::mutex> lock(device_mutex_);
    port_io_ = PortIo(std::move(port_read), std::move(port_write), 0);
    InitFifosLocked();
  }

  auto status = SerialImplConfig(kMaxBaudRate, kDefaultConfig);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SerialImplConfig failed\n", __func__);
    return status;
  }

  interrupt_thread_ = std::thread([&] { HandleInterrupts(); });

  return ZX_OK;
}

zx::unowned_interrupt Uart16550::InterruptHandle() { return zx::unowned_interrupt(interrupt_); }

zx_status_t Uart16550::SerialImplGetInfo(serial_port_info_t* info) {
  *info = kInfo;
  return ZX_OK;
}

zx_status_t Uart16550::SerialImplConfig(uint32_t baud_rate, uint32_t flags) {
  if (Enabled()) {
    zxlogf(ERROR, "%s: attempted to configure when enabled\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

  if (baud_rate == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  const auto divisor = static_cast<uint16_t>(kMaxBaudRate / baud_rate);
  if (divisor != kMaxBaudRate / baud_rate || divisor == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((flags & SERIAL_FLOW_CTRL_MASK) != SERIAL_FLOW_CTRL_NONE && !SupportsAutomaticFlowControl()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const auto lower = static_cast<uint8_t>(divisor);
  const auto upper = static_cast<uint8_t>(divisor >> 8);

  std::lock_guard<std::mutex> lock(device_mutex_);

  auto lcr = LineControlRegister::Get().ReadFrom(&port_io_);

  lcr.set_divisor_latch_access(true).WriteTo(&port_io_);

  DivisorLatchLowerRegister::Get().FromValue(0).set_data(lower).WriteTo(&port_io_);
  DivisorLatchUpperRegister::Get().FromValue(0).set_data(upper).WriteTo(&port_io_);

  lcr.set_divisor_latch_access(false);

  if (flags & SERIAL_SET_BAUD_RATE_ONLY) {
    lcr.WriteTo(&port_io_);
    return ZX_OK;
  }

  switch (flags & SERIAL_DATA_BITS_MASK) {
    case SERIAL_DATA_BITS_5:
      lcr.set_word_length(LineControlRegister::kWordLength5);
      break;
    case SERIAL_DATA_BITS_6:
      lcr.set_word_length(LineControlRegister::kWordLength6);
      break;
    case SERIAL_DATA_BITS_7:
      lcr.set_word_length(LineControlRegister::kWordLength7);
      break;
    case SERIAL_DATA_BITS_8:
      lcr.set_word_length(LineControlRegister::kWordLength8);
      break;
  }

  switch (flags & SERIAL_STOP_BITS_MASK) {
    case SERIAL_STOP_BITS_1:
      lcr.set_stop_bits(LineControlRegister::kStopBits1);
      break;
    case SERIAL_STOP_BITS_2:
      lcr.set_stop_bits(LineControlRegister::kStopBits2);
      break;
  }

  switch (flags & SERIAL_PARITY_MASK) {
    case SERIAL_PARITY_NONE:
      lcr.set_parity_enable(false);
      lcr.set_even_parity(false);
      break;
    case SERIAL_PARITY_ODD:
      lcr.set_parity_enable(true);
      lcr.set_even_parity(false);
      break;
    case SERIAL_PARITY_EVEN:
      lcr.set_parity_enable(true);
      lcr.set_even_parity(true);
      break;
  }

  lcr.WriteTo(&port_io_);

  auto mcr = ModemControlRegister::Get().FromValue(0);

  // The below is necessary for interrupts on some devices.
  mcr.set_auxiliary_out_2(true);

  switch (flags & SERIAL_FLOW_CTRL_MASK) {
    case SERIAL_FLOW_CTRL_NONE:
      mcr.set_automatic_flow_control_enable(false);
      mcr.set_data_terminal_ready(true);
      mcr.set_request_to_send(true);
      break;
    case SERIAL_FLOW_CTRL_CTS_RTS:
      mcr.set_automatic_flow_control_enable(true);
      mcr.set_data_terminal_ready(false);
      mcr.set_request_to_send(false);
      break;
  }

  mcr.WriteTo(&port_io_);

  return ZX_OK;
}

zx_status_t Uart16550::SerialImplEnable(bool enable) {
  std::lock_guard<std::mutex> lock(device_mutex_);
  if (enabled_) {
    if (!enable) {
      // The device is enabled, and will be disabled.
      InterruptEnableRegister::Get()
          .FromValue(0)
          .set_rx_available(false)
          .set_line_status(false)
          .set_modem_status(false)
          .set_tx_empty(false)
          .WriteTo(&port_io_);
    }
  } else {
    if (enable) {
      // The device is disabled, and will be enabled.
      ResetFifosLocked();
      InterruptEnableRegister::Get()
          .FromValue(0)
          .set_rx_available(true)
          .set_line_status(true)
          .set_modem_status(true)
          .set_tx_empty(false)
          .WriteTo(&port_io_);
    }
  }
  enabled_ = enable;
  return ZX_OK;
}

zx_status_t Uart16550::SerialImplRead(void* buf, size_t size, size_t* actual) {
  std::lock_guard<std::mutex> lock(device_mutex_);
  *actual = 0;

  if (!enabled_) {
    zxlogf(ERROR, "%s: attempted to read when disabled\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

  auto p = static_cast<uint8_t*>(buf);

  auto lcr = LineStatusRegister::Get();

  auto data_ready_and_notify = [&]() __TA_REQUIRES(device_mutex_) {
    auto ready = lcr.ReadFrom(&port_io_).data_ready();
    auto state = state_;
    if (!ready) {
      state &= ~SERIAL_STATE_READABLE;
    } else {
      state |= SERIAL_STATE_READABLE;
    }
    if (state_ != state) {
      state_ = state;
      NotifyLocked();
    }
    return ready;
  };

  if (!data_ready_and_notify()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  auto rbr = RxBufferRegister::Get();

  while (data_ready_and_notify() && size != 0) {
    *p++ = rbr.ReadFrom(&port_io_).data();
    *actual += 1;
    --size;
  }

  return ZX_OK;
}

zx_status_t Uart16550::SerialImplWrite(const void* buf, size_t size, size_t* actual) {
  std::lock_guard<std::mutex> lock(device_mutex_);
  *actual = 0;

  if (!enabled_) {
    zxlogf(ERROR, "%s: attempted to write when disabled\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

  auto p = static_cast<const uint8_t*>(buf);
  size_t writable = std::min(size, uart_fifo_len_);

  auto lsr = LineStatusRegister::Get();
  auto ier = InterruptEnableRegister::Get();

  if (!lsr.ReadFrom(&port_io_).tx_empty()) {
    ier.ReadFrom(&port_io_).set_tx_empty(true).WriteTo(&port_io_);
    return ZX_ERR_SHOULD_WAIT;
  }

  auto tbr = TxBufferRegister::Get();

  while (writable != 0) {
    tbr.FromValue(0).set_data(*p++).WriteTo(&port_io_);
    *actual += 1;
    --writable;
  }

  if (*actual != size) {
    ier.ReadFrom(&port_io_).set_tx_empty(true).WriteTo(&port_io_);
  }

  if (*actual != 0) {
    auto state = state_;
    state &= ~SERIAL_STATE_WRITABLE;
    if (state_ != state) {
      state_ = state;
      NotifyLocked();
    }
  }

  return ZX_OK;
}

zx_status_t Uart16550::SerialImplSetNotifyCallback(const serial_notify_t* cb) {
  std::lock_guard<std::mutex> lock(device_mutex_);
  if (enabled_) {
    zxlogf(ERROR, "%s: attempted to set notify callback when enabled\n", __func__);
    return ZX_ERR_BAD_STATE;
  }

  if (!cb) {
    notify_cb_.callback = nullptr;
    notify_cb_.ctx = nullptr;
  } else {
    notify_cb_ = *cb;
  }

  return ZX_OK;
}

void Uart16550::DdkRelease() {
  SerialImplEnable(false);
  // End the interrupt loop by canceling waits.
  interrupt_.destroy();
  interrupt_thread_.join();
  delete this;
}

void Uart16550::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

bool Uart16550::SupportsAutomaticFlowControl() const { return uart_fifo_len_ == kFifoDepth16750; }

void Uart16550::ResetFifosLocked() {
  // 16750 requires we toggle extended fifo while divisor latch is enabled.
  LineControlRegister::Get().FromValue(0).set_divisor_latch_access(true).WriteTo(&port_io_);
  FifoControlRegister::Get()
      .FromValue(0)
      .set_fifo_enable(true)
      .set_rx_fifo_reset(true)
      .set_tx_fifo_reset(true)
      .set_dma_mode(0)
      .set_extended_fifo_enable(true)
      .set_reciever_trigger(FifoControlRegister::kMaxTriggerLevel)
      .WriteTo(&port_io_);
  LineControlRegister::Get().FromValue(0).set_divisor_latch_access(false).WriteTo(&port_io_);
}

void Uart16550::InitFifosLocked() {
  ResetFifosLocked();
  const auto iir = InterruptIdentRegister::Get().ReadFrom(&port_io_);
  if (iir.fifos_enabled()) {
    if (iir.extended_fifo_enabled()) {
      uart_fifo_len_ = kFifoDepth16750;
    } else {
      uart_fifo_len_ = kFifoDepth16550A;
    }
  } else {
    uart_fifo_len_ = kFifoDepthGeneric;
  }
}

void Uart16550::NotifyLocked() {
  if (notify_cb_.callback && enabled_) {
    notify_cb_.callback(notify_cb_.ctx, state_);
  }
}

// Loop and wait on the interrupt handle. When an interrupt is detected, read the interrupt
// identifier. If there is data available in the hardware RX FIFO, notify readable. If the
// hardware TX FIFO is empty, notify writable. If there is a line status error, log it. If
// there is a modem status, log it.
void Uart16550::HandleInterrupts() {
  // Ignore the timestamp.
  while (interrupt_.wait(nullptr) == ZX_OK) {
    std::lock_guard<std::mutex> lock(device_mutex_);

    if (!enabled_) {
      // Interrupts should be disabled now and we shouldn't respond to them.
      continue;
    }

    const auto identifier = InterruptIdentRegister::Get().ReadFrom(&port_io_).interrupt_id();

    switch (static_cast<InterruptType>(identifier)) {
      case InterruptType::kNone:
        break;
      case InterruptType::kRxLineStatus: {
        // Clear the interrupt.
        const auto lsr = LineStatusRegister::Get().ReadFrom(&port_io_);
        if (lsr.overrun_error()) {
          zxlogf(ERROR, "%s: overrun error (OE) detected\n", __func__);
        }
        if (lsr.parity_error()) {
          zxlogf(ERROR, "%s: parity error (PE) detected\n", __func__);
        }
        if (lsr.framing_error()) {
          zxlogf(ERROR, "%s: framing error (FE) detected\n", __func__);
        }
        if (lsr.break_interrupt()) {
          zxlogf(ERROR, "%s: break interrupt (BI) detected\n", __func__);
        }
        if (lsr.error_in_rx_fifo()) {
          zxlogf(ERROR, "%s: error in rx fifo detected\n", __func__);
        }
        break;
      }
      case InterruptType::kRxDataAvailable:  // In both cases, there is data ready in the rx fifo.
      case InterruptType::kCharTimeout: {
        auto state = state_;
        state |= SERIAL_STATE_READABLE;
        if (state_ != state) {
          state_ = state;
          NotifyLocked();
        }
        break;
      }
      case InterruptType::kTxEmpty: {
        InterruptEnableRegister::Get().ReadFrom(&port_io_).set_tx_empty(false).WriteTo(&port_io_);
        auto state = state_;
        state |= SERIAL_STATE_WRITABLE;
        if (state_ != state) {
          state_ = state;
          NotifyLocked();
        }
        break;
      }
      case InterruptType::kModemStatus: {
        // Clear the interrupt.
        const auto msr = ModemStatusRegister::Get().ReadFrom(&port_io_);
        if (msr.clear_to_send()) {
          zxlogf(INFO, "%s: clear to send (CTS) detected\n", __func__);
        }
        if (msr.data_set_ready()) {
          zxlogf(INFO, "%s: data set ready (DSR) detected\n", __func__);
        }
        if (msr.ring_indicator()) {
          zxlogf(INFO, "%s: ring indicator (RI) detected\n", __func__);
        }
        if (msr.data_carrier_detect()) {
          zxlogf(INFO, "%s: data carrier (DCD) detected\n", __func__);
        }
        break;
      }
    }
  }
}

static constexpr zx_driver_ops_t driver_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Uart16550::Create;
  return ops;
}();

}  // namespace uart16550

// clang-format off
ZIRCON_DRIVER_BEGIN(uart16550, uart16550::driver_ops, "zircon", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ACPI),
  BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, 0x504e5030), // PNP0501\0
  BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x35303100),
ZIRCON_DRIVER_END(uart16550)
