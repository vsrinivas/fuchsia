// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/fusb302/fusb302.h"

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/zx/profile.h>
#include <zircon/threads.h>

#include <fbl/alloc_checker.h>

#include "src/devices/power/drivers/fusb302/fusb302-bind.h"
#include "src/devices/power/drivers/fusb302/registers.h"

namespace fusb302 {

using usb::pd::Header;
using usb::pd::kMaxLen;
using PdMessageType = usb::pd::PdMessage::PdMessageType;
using ControlMessageType = usb::pd::ControlPdMessage::ControlMessageType;
using PowerType = usb::pd::DataPdMessage::PowerType;
using FixedSupplyPDO = usb::pd::DataPdMessage::FixedSupplyPDO;

namespace {

// Sleep after setting measure bits and before taking measurements to give time to hardware to
// react.
auto constexpr tMeasureSleep = zx::usec(300);

}  // namespace

zx::status<Event> Fusb302::GetInterrupt() {
  Event event(0);
  zx_status_t status;

  //  Read interrupts
  auto interrupt = InterruptReg::ReadFrom(i2c_);
  auto interrupt_a = InterruptAReg::ReadFrom(i2c_);
  auto interrupt_b = InterruptBReg::ReadFrom(i2c_);
  zxlogf(DEBUG, "Received interrupt: Interrupt 0x%x, InterruptA 0x%x, InterruptB 0x%x",
         interrupt.reg_value(), interrupt_a.reg_value(), interrupt_b.reg_value());

  if (interrupt.i_bc_lvl() || interrupt.i_vbusok()) {
    if (is_cc_connected_) {
      event.set_cc(true);
    }
  }

  if (interrupt_a.i_togdone()) {
    event.set_cc(true);
    auto cc_state = Status1AReg::ReadFrom(i2c_).togss();
    power_role_ = Status1AReg::GetPowerRole(cc_state);
    polarity_ = Status1AReg::GetPolarity(cc_state);

    status = Control2Reg::ReadFrom(i2c_).set_toggle(0).WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
      return zx::error(status);
    }

    status = Switches0Reg::ReadFrom(i2c_)
                 .set_pu_en1(power_role_ == source)
                 .set_pu_en2(power_role_ == source)
                 .set_pdwn1(power_role_ == sink)
                 .set_pdwn2(power_role_ == sink)
                 .WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
      return zx::error(status);
    }
  }

  if (interrupt_b.i_gcrcsent()) {
    event.set_rx(true);
  }

  if (interrupt_a.i_txsent()) {
    // First treat this as an rx event. After receiving the message and checking if it's a GOOD_CRC,
    // we will modify the event correspondingly.
    event.set_rx(true);
  }

  if (interrupt_a.i_hardrst()) {
    status = ResetReg::Get().FromValue(0x0).set_pd_reset(1).WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Could not reset. %d", status);
      return zx::error(status);
    }
    event.set_rec_reset(true);
  }

  if (interrupt_a.i_retryfail()) {
    event.set_tx(true);
    tx_state_ = failed;
  }

  if (interrupt_a.i_hardsent()) {
    tx_state_ = success;
    event.set_tx(true);
  }

  return zx::ok(event);
}

zx_status_t Fusb302::IrqThread() {
  zx_status_t status = ZX_OK;

  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    constexpr zx::duration capacity = zx::msec(3);
    constexpr zx::duration deadline = zx::msec(4);
    constexpr zx::duration period = deadline;

    zx::profile profile;
    status = device_get_deadline_profile(parent_, capacity.get(), deadline.get(), period.get(),
                                         "fusb302_profile", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "Failed to get deadline profile: %s", zx_status_get_string(status));
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(irq_thread_), profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARNING, "Failed to apply deadline profile: %s", zx_status_get_string(status));
      }
    }
  }

  while (true) {
    zx_port_packet_t packet;
    status = port_.wait(zx::time(ZX_TIME_INFINITE), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Port wait failed");
      break;
    }

    Event event(0);
    std::shared_ptr<PdMessage> message;
    switch (packet.key) {
      case kInterrupt: {
        auto val = GetInterrupt();
        if (val.is_error()) {
          zxlogf(ERROR, "Couldn't handle interrupt %s", val.status_string());
          status = val.status_value();
          break;
        }
        event = val.value();
        zxlogf(DEBUG, "event %x", event.value);

        if (is_cc_connected_ && (event.cc())) {
          if (power_role_ == sink) {
            if (!Status0Reg::ReadFrom(i2c_).vbusok()) {
              InitHw();
              state_machine_.Restart();
            }
          } else {
            uint8_t cc1, cc2;
            status = GetCC(&cc1, &cc2);
            if (status != ZX_OK) {
              zxlogf(ERROR, "Failed to get CC. %d", status);
              break;
            }
            if (polarity_ == CC2) {
              cc1 = cc2;
            }
            if (cc1 == 0) {
              InitHw();
              state_machine_.Restart();
            }
          }
        }

        if (event.rx()) {
          auto val = FifoReceive();
          if (val.is_error()) {
            zxlogf(ERROR, "Could not receive message. %s", val.status_string());
            status = val.status_value();
            break;
          }
          // Because RX and TX events could be received out of order, check here and modify event
          // flags instead.
          if ((val.value().GetPdMessageType() == PdMessageType::CONTROL) &&
              (val.value().header().message_type() == ControlMessageType::GOOD_CRC)) {
            // Received GOOD_CRC message. Should be a TX event.
            event.set_tx(true);
            event.set_rx(false);
            tx_state_ = success;
          } else {
            message = std::make_shared<PdMessage>(std::move(val.value()));
          }
        }

        if ((event.tx()) && (tx_state_ == success)) {
          message_id_++;
        }
        break;
      }
      case kTimer: {
        break;
      }
      default:
        zxlogf(ERROR, "Unrecognized packet key: %lu", packet.key);
        status = ZX_ERR_INTERNAL;
        break;
    }

    status = state_machine_.Run(event, std::move(message));
    if (status != ZX_OK) {
      zxlogf(ERROR, "State machine failed with %d", status);
      break;
    }

    if (packet.key == kInterrupt) {
      status = irq_.ack();
      if (status != ZX_OK) {
        zxlogf(ERROR, "Ack IRQ failed with %d", status);
        break;
      }
    }
  }
  is_thread_running_ = false;
  zxlogf(ERROR, "IRQ thread failed with %d", status);
  return status;
}

zx_status_t Fusb302::FifoTransmit(const PdMessage& message) {
  if (tx_state_ == busy) {
    return ZX_ERR_SHOULD_WAIT;
  }
  enum TxToken : uint8_t {
    kTxOn = 0xA1,
    kSOP1 = 0x12,
    kSOP2 = 0x13,
    kSOP3 = 0x1B,
    kReset1 = 0x15,
    kReset2 = 0x16,
    kPackSym = 0x80,
    kJamCrc = 0xFF,
    kEOP = 0x14,
    kTxOff = 0xFE,
  };

  uint8_t buf[11 + message.header().num_data_objects() * 4];
  buf[0] = kSOP1;
  buf[1] = kSOP1;
  buf[2] = kSOP1;
  buf[3] = kSOP2;  // SOP
  buf[4] = kPackSym | (message.header().num_data_objects() * 4 + 2);

  buf[5] = message.header().value & 0xFF;
  buf[6] = (message.header().value >> 8) & 0xFF;

  // Data
  memcpy(&buf[7], message.payload().data(), message.header().num_data_objects() * 4);

  buf[7 + message.header().num_data_objects() * 4] = kJamCrc;
  buf[8 + message.header().num_data_objects() * 4] = kEOP;
  buf[9 + message.header().num_data_objects() * 4] = kTxOff;
  buf[10 + message.header().num_data_objects() * 4] = kTxOn;

  // Specs say WriteSync is supported, but it doesn't work.
  for (size_t i = 0; i < sizeof(buf); i++) {
    auto status = FifosReg::Get().FromValue(buf[i]).WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Could not transmit %zu", i);
      return status;
    }
  }
  tx_state_ = busy;
  return ZX_OK;
}

zx::status<PdMessage> Fusb302::FifoReceive() {
  enum RxToken : uint8_t {
    kRxSop = 0b111,
    kRxSop1 = 0b110,
    kRxSop2 = 0b101,
    kRxSop1Db = 0b100,
    kRxSop2Db = 0b011,
  };

  auto sop = FifosReg::ReadFrom(i2c_).reg_value();
  if ((sop >> 5) != kRxSop) {
    zxlogf(ERROR, "Invalid SOP token 0x%x", sop >> 5);
    return zx::error(ZX_ERR_INTERNAL);
  }
  // header
  uint16_t header_val = FifosReg::ReadFrom(i2c_).reg_value() & 0xFF;
  header_val |= (FifosReg::ReadFrom(i2c_).reg_value() & 0xFF) << 8;
  Header header(header_val);
  // read message
  if (header.num_data_objects() * 4 > kMaxLen) {
    zxlogf(ERROR, "Buffer not large enough");
    return zx::error(ZX_ERR_INTERNAL);
  }
  uint8_t data[header.num_data_objects() * 4];
  // Specs say ReadSync is supported, but it doesn't work.
  for (size_t i = 0; i < header.num_data_objects() * 4; i++) {
    data[i] = FifosReg::ReadFrom(i2c_).reg_value();
  }
  // CRC
  uint32_t crc = FifosReg::ReadFrom(i2c_).reg_value();
  crc |= FifosReg::ReadFrom(i2c_).reg_value() << (8 * 1);
  crc |= FifosReg::ReadFrom(i2c_).reg_value() << (8 * 2);
  crc |= FifosReg::ReadFrom(i2c_).reg_value() << (8 * 3);

  // Update message id
  message_id_ = header.message_id();
  return zx::ok(PdMessage(header_val, &data[0]));
}

zx_status_t Fusb302::GetCC(uint8_t* cc1, uint8_t* cc2) {
  auto save = Switches0Reg::ReadFrom(i2c_).reg_value();  // save
  *cc1 = MeasureCC(CC1);
  *cc2 = MeasureCC(CC2);
  return Switches0Reg::Get().FromValue(save).WriteTo(i2c_);  // restore
}

uint8_t Fusb302::MeasureCC(Polarity polarity) {
  if (power_role_ != sink) {
    // Only sink operations allowed for now. Implement source when the need arises.
    zxlogf(ERROR, "Can't measure for source!");
    return 0;
  }

  auto status = Switches0Reg::ReadFrom(i2c_)
                    .set_meas_cc1(polarity == CC1)
                    .set_meas_cc2(polarity == CC2)
                    .set_pu_en1(0)
                    .set_pu_en2(0)
                    .set_pdwn1(1)
                    .set_pdwn2(1)
                    .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(tMeasureSleep));

  return Status0Reg::ReadFrom(i2c_).bc_lvl();
}

zx_status_t Fusb302::Debounce() {
  uint32_t count = 10, debounce_count = 0;
  uint8_t old_cc1, old_cc2;
  auto status = GetCC(&old_cc1, &old_cc2);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get CC. %d", status);
    return status;
  }

  while (count--) {
    uint8_t cc1, cc2;
    status = GetCC(&cc1, &cc2);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to get CC. %d", status);
      return status;
    }

    if ((cc1 == old_cc1) && (cc2 == old_cc2)) {
      debounce_count++;
    } else {
      old_cc1 = cc1;
      old_cc2 = cc2;
      debounce_count = 0;
    }

    zx::nanosleep(zx::deadline_after(zx::usec(2000)));
    if (debounce_count > 9) {
      if ((old_cc1 != old_cc2) && (!old_cc1 || !old_cc2)) {
        return ZX_OK;
      }
    }
  }
  return ZX_ERR_INTERNAL;
}

zx_status_t Fusb302::SetPolarity(Polarity polarity) {
  auto status = Switches0Reg::ReadFrom(i2c_)
                    .set_meas_cc1(polarity == CC1)
                    .set_meas_cc2(polarity == CC2)
                    .set_vconn_cc1(0)
                    .set_vconn_cc2(0)
                    .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  status = Switches1Reg::ReadFrom(i2c_)
               .set_txcc1(polarity == CC1)
               .set_txcc2(polarity == CC2)
               .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  polarity_ = polarity;
  return ZX_OK;
}

zx_status_t Fusb302::SetCC(DataRole mode) {
  auto switches0 =
      Switches0Reg::ReadFrom(i2c_).set_pdwn1(0).set_pdwn2(0).set_pu_en1(0).set_pu_en2(0);
  switch (mode) {
    // Only sink operations allowed for now. Implement source when the need arises.
    case UFP:
    case DRP:
      switches0.set_pdwn1(1).set_pdwn2(1);
      break;
    default:
      zxlogf(ERROR, "Unsupported mode %u", mode);
      return ZX_ERR_INTERNAL;
  }
  auto status = switches0.WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Fusb302::RxEnable(bool enable) {
  zx_status_t status;
  if (enable) {
    status = Switches0Reg::ReadFrom(i2c_)
                 .set_meas_cc1(polarity_ == CC1)
                 .set_meas_cc2(polarity_ == CC2)
                 .WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
      return status;
    }

    status = Control1Reg::ReadFrom(i2c_).set_rx_flush(1).WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to flush. %d", status);
      return status;
    }
  } else {
    status = SetCC(DRP);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to Set CC to DRP %d", status);
      return status;
    }

    status = Control2Reg::ReadFrom(i2c_).set_tog_rd_only(1).WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
      return status;
    }

    status = Switches0Reg::ReadFrom(i2c_).set_meas_cc1(0).set_meas_cc2(0).WriteTo(i2c_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
      return status;
    }
  }

  status = Switches1Reg::ReadFrom(i2c_).set_auto_crc(enable).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Fusb302::InitHw() {
  // Reset
  auto status = ResetReg::ReadFrom(i2c_).set_sw_res(1).set_pd_reset(1).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  // Enable TX Auto retries
  status = Control3Reg::ReadFrom(i2c_).set_n_retries(3).set_auto_retry(1).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  // Init Interrupt
  status = MaskReg::Get()
               .FromValue(0xFF)
               .set_m_bc_lvl(0)
               .set_m_collision(0)
               .set_m_alert(0)
               .set_m_vbusok(0)
               .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  status = MaskAReg::Get()
               .FromValue(0xFF)
               .set_m_togdone(0)
               .set_m_retryfail(0)
               .set_m_hardsent(0)
               .set_m_txsent(0)
               .set_m_hardrst(0)
               .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  status = MaskBReg::Get().FromValue(0xFF).set_m_gcrcsent(0).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  // Start DRP toggling
  status = Control2Reg::ReadFrom(i2c_)
               .set_mode(Control2Reg::ENABLE_DRP)
               .set_toggle(1)
               .set_tog_rd_only(1)
               .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }

  // Set Host Current and Enable Interrupts
  status = Control0Reg::ReadFrom(i2c_)
               .set_host_cur(Control0Reg::MEDIUM_1A5)
               .set_int_mask(0)
               .WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }

  // Set polarity
  status = SetPolarity(CC1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to set polarity. %d", status);
    return status;
  }

  // Set Power Mode
  status = PowerReg::Get().FromValue(0x0F).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  status = RxEnable(false);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't disable RX. %d", status);
    return status;
  }

  status = SetCC(DRP);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Couldn't set CC as DRP. %d", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Fusb302::Init() {
  // InitInspect also initializes variables for state machine and DRP
  auto status = InitInspect();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialize inspect. %d", status);
    return status;
  }

  status = InitHw();
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitHw failed. %d", status);
    return status;
  }

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port_create failed: %d", __FILE__, status);
    return status;
  }
  irq_.bind(port_, kInterrupt, 0);
  status = thrd_status_to_zx_status(thrd_create_with_name(
      &irq_thread_, [](void* ctx) -> int { return reinterpret_cast<Fusb302*>(ctx)->IrqThread(); },
      this, "fusb302_thread"));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to start thread: %s", zx_status_get_string(status));
    return status;
  }
  is_thread_running_ = true;

  return ZX_OK;
};

zx_status_t Fusb302::InitInspect() {
  // Device ID
  auto device_id = DeviceIdReg::ReadFrom(i2c_);

  device_id_ = inspect_.GetRoot().CreateChild("DeviceId");
  device_id_.CreateUint("VersionId", device_id.version_id(), &inspect_);
  device_id_.CreateUint("ProductId", device_id.product_id(), &inspect_);
  device_id_.CreateUint("RevisionId", device_id.revision_id(), &inspect_);

  return ZX_OK;
}

zx_status_t Fusb302::Create(void* context, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get I2C");
    return ZX_ERR_INTERNAL;
  }

  ddk::GpioProtocolClient gpio(parent, "gpio");
  if (!gpio.is_valid()) {
    zxlogf(ERROR, "Failed to get GPIO");
    return ZX_ERR_INTERNAL;
  }
  auto status = gpio.ConfigIn(GPIO_PULL_UP);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ConfigIn failed, status = %d", status);
  }
  zx::interrupt irq;
  status = gpio.GetInterrupt(ZX_INTERRUPT_MODE_LEVEL_LOW, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "GetInterrupt failed, status = %d", status);
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Fusb302> device(new (&ac) Fusb302(parent, i2c, std::move(irq)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = device->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Init failed, status = %d", status);
  }

  status = device->DdkAdd(
      ddk::DeviceAddArgs("fusb302").set_inspect_vmo(device->inspect_.DuplicateVmo()));
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed, status = %d", status);
  }

  // Let device runner take ownership of this object.
  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

}  // namespace fusb302

static constexpr zx_driver_ops_t fusb302_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = fusb302::Fusb302::Create;
  return result;
}();

// clang-format off
ZIRCON_DRIVER(fusb302, fusb302_driver_ops, "zircon", "0.1");

