// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/fusb302/fusb302.h"
#include "src/devices/power/drivers/fusb302/registers.h"

namespace fusb302 {

using PdMessageType = usb::pd::PdMessage::PdMessageType;
using usb::pd::ControlPdMessage;
using ControlMessageType = usb::pd::ControlPdMessage::ControlMessageType;
using usb::pd::DataPdMessage;
using DataMessageType = usb::pd::DataPdMessage::DataMessageType;
using PowerType = usb::pd::DataPdMessage::PowerType;
using FixedSupplyPDO = usb::pd::DataPdMessage::FixedSupplyPDO;
using FixedVariableSupplyRDO = usb::pd::DataPdMessage::FixedVariableSupplyRDO;

namespace {

// Timeout of sink waiting for source capabilities.
auto constexpr tSinkWaitCapTimer = zx::msec(2'500);

}  // namespace

zx_status_t SinkPolicyEngine::Init() {
  auto status = zx::timer::create(ZX_TIMER_SLACK_CENTER, ZX_CLOCK_MONOTONIC, &sink_wait_cap_timer_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create timer: %d", status);
    return status;
  }
  if (initialized_) {
    // Because USB-PD protocol was already started in bootloader, messages have already been
    // sent. Initialization is already done and message_id_ is at least 3. Start sending
    // GET_SOURCE_CAP messages and test for a response with message_id_ = 3 (increasing).
    // Messages with message_id_ less than what the other end expects will be GOODCRCed and
    // might be ignored. So, we will send message with message_id_ = 6 (which is one less than
    // max message_id_), and update message_id_ when the source responds and tells us which
    // message_id_ it's at.
    device()->message_id_ = 6;
    // When USB-PD is set up by bootloader, it will stop (and wait) at the pe_snk_ready state.
    // Use a GET_SOURCE_CAP message to obtain the source capabilities and save the
    // capabilities for further requests.
    SetState(pe_snk_get_source_cap);
  } else {
    // This should never happen because Fuchsia currently does all USB-PD initialization logic
    // in the bootloader.
    SetState(pe_snk_startup);
  }
  return ZX_OK;
}

uint8_t SinkPolicyEngine::FindPdo(uint32_t max_voltage_mV, uint32_t max_current_mA) {
  for (size_t i = source_capabilities_.size() - 1; i >= 0; i--) {
    const auto& pdo = source_capabilities_.get(i);
    switch (pdo.power_type()) {
      case PowerType::FIXED_SUPPLY: {
        FixedSupplyPDO fixed(pdo.value);
        auto volt = fixed.voltage_50mV() * 50;
        auto curr = fixed.maximum_current_10mA() * 10;
        if ((volt <= max_voltage_mV) && (curr <= max_current_mA)) {
          zxlogf(
              INFO,
              "Found source capability %zu with voltage %u mV and maximum current %u mA satisfying"
              " requested max moltage %u mV and maximum current %u mA",
              i + 1, volt, curr, max_voltage_mV, max_current_mA);
          return i + 1;
        }
        break;
      }
      // Only for fixed supply supported for now. Implement others as the need arises.
      default:
        zxlogf(ERROR, "Invalid power type %u", pdo.power_type());
    }
  }
  return UINT8_MAX;
}

// For Sink Policy Engine State Machine Transitions, see Universal Serial Bus Power Delivery
// Specification: Policy Engine Sink Port State Diagram (Section 8.3.3.3 or Figure 8-44 in
// Revision 2.0 Version 1.3)
zx_status_t SinkPolicyEngine::RunState(Event event, std::shared_ptr<PdMessage> message,
                                       bool entry) {
  zx_status_t status;
  switch (state()) {
    // Note: Many states, transition, actions, and timers are not implemented. Implement them as
    // the need comes up.
    case pe_snk_startup:
    case pe_snk_discovery:
    case pe_snk_wait_for_capabilities: {
      // These states are currently not used because initialized_ is always true when Fusb302
      // boots up (meaning that the bootloader has already done the initial exchange). Implement
      // these when we need them.
      zxlogf(ERROR, "Unreachable state %u", state());
      return ZX_ERR_INTERNAL;
    }
    case pe_snk_evaluate_capability: {
      // Upon entering this state, we should have just received a message.
      ZX_DEBUG_ASSERT(event.rx());
      if (!message) {
        zxlogf(ERROR, "Incompatible. There should be a non-null message");
        return ZX_ERR_INTERNAL;
      }
      // Save PDOs
      const auto& payload = message->payload();
      for (size_t i = 0; i < message->header().num_data_objects(); i++) {
        source_capabilities_.emplace_back((payload[i * 4 + 0]) | (payload[i * 4 + 1] << 8) |
                                          (payload[i * 4 + 2] << 16) | (payload[i * 4 + 3] << 24));
      }
      // Evaluate capabilities and find one satisfying requirements
      curr_object_position_.set(
          FindPdo(requested_max_volt_mV_.get(), requested_max_curr_mA_.get()));
      SetState(pe_snk_select_capability);
      break;
    }
    case pe_snk_select_capability: {
      // Actions
      if (entry) {
        uint32_t rdo_val;
        switch (source_capabilities_.get(curr_object_position_.get() - 1).power_type()) {
          case PowerType::FIXED_SUPPLY: {
            auto max_curr =
                FixedSupplyPDO(source_capabilities_.get(curr_object_position_.get() - 1).value)
                    .maximum_current_10mA();
            FixedVariableSupplyRDO fixed(0);
            fixed.set_operating_current_10mA(0)
                .set_maximum_current_10mA(max_curr)
                // Note: FixedVariableSupplyRDO variables should come before RequestDataObject
                // variables
                .set_object_position(curr_object_position_.get())
                .set_give_back(false)
                .set_capability_mismatch(true)
                .set_usb_communications_capable(false)
                .set_no_usb_suspend(true)
                .set_unchunked_extended_messages_supported(false);
            rdo_val = fixed.value;
            break;
          }
          // Only for fixed supply supported for now. Implement others as the need arises.
          default:
            zxlogf(ERROR, "Unsupported Source type %u",
                   source_capabilities_.get(curr_object_position_.get() - 1).power_type());
            return ZX_ERR_INTERNAL;
        }
        uint8_t payload[4] = {static_cast<uint8_t>(rdo_val & 0xFF),
                              static_cast<uint8_t>((rdo_val >> 8) & 0xFF),
                              static_cast<uint8_t>((rdo_val >> 16) & 0xFF),
                              static_cast<uint8_t>((rdo_val >> 24) & 0xFF)};
        auto req = DataPdMessage(/* num_data_objects */ 1, device()->message_id_,
                                 device()->power_role_.get(), device()->spec_rev_.get(),
                                 device()->data_role_.get(), DataMessageType::REQUEST, payload);
        do {
          status = device()->FifoTransmit(req);
        } while (status == ZX_ERR_SHOULD_WAIT);
        if (status != ZX_OK) {
          zxlogf(ERROR, "FifoTransmit failed %d", status);
          return status;
        }
        break;
      }

      // Transitions
      if (event.rx()) {
        if (!message) {
          zxlogf(ERROR, "Incompatible. There should be a non-null message");
          return ZX_ERR_INTERNAL;
        }
        if ((message->GetPdMessageType() == PdMessageType::CONTROL) &&
            (message->header().message_type() == ControlMessageType::ACCEPT)) {
          SetState(pe_snk_transition_sink);
        }
      }
      break;
    }
    case pe_snk_transition_sink: {
      // Transitions
      if (event.rx()) {
        if (!message) {
          zxlogf(ERROR, "Incompatible. There should be a non-null message");
          return ZX_ERR_INTERNAL;
        }
        if ((message->GetPdMessageType() == PdMessageType::CONTROL) &&
            (message->header().message_type() == ControlPdMessage::PS_RDY)) {
          SetState(pe_snk_ready);
        }
      }
      break;
    }
    case pe_snk_ready: {
      if (entry) {
        if (initialized_) {
          // Start SinkWaitCapTimer for first time GET_SOURCE_CAP is sent. If no response is found
          // within time limit, USB-PD is not supported.
          sink_wait_cap_timer_.cancel();
          status = sink_wait_cap_timer_.set(zx::deadline_after(tSinkWaitCapTimer), zx::duration(0));
          sink_wait_cap_timer_.wait_async(device()->port_, kTimer, ZX_TIMER_SIGNALED, 0);
          initialized_ = false;
        }
      }

      // Transitions
      // TODO (rdzhuang): also accept requests from FIDL
      if (event.rx()) {
        if (!message) {
          zxlogf(ERROR, "Incompatible. There should be a non-null message");
          return ZX_ERR_INTERNAL;
        }
        // Received SOURCE_CAPABILTIES
        if ((message->GetPdMessageType() == PdMessageType::DATA) &&
            (message->header().message_type() == DataMessageType::SOURCE_CAPABLILITIES)) {
          // Stop SinkWaitCapTimer
          sink_wait_cap_timer_.cancel();
          SetState(pe_snk_evaluate_capability);
        }
      }
      // SinkWaitCapTimer ran out
      if (sink_wait_cap_timer_.wait_one(ZX_TIMER_SIGNALED, zx::time(0), nullptr) == ZX_OK) {
        zxlogf(ERROR,
               "SinkWaitCapTimer time is up. Source has not responded to GET_SOURCE_CAP message. "
               "Source does not support USB-PD. Quitting IRQ thread.");
        sink_wait_cap_timer_.cancel();
        return ZX_ERR_TIMED_OUT;
      }
      break;
    }
    case pe_snk_get_source_cap: {
      if (entry) {
        zxlogf(DEBUG, "Sending message with id %u", device()->message_id_);
        auto req = ControlPdMessage(device()->message_id_, device()->power_role_.get(),
                                    device()->spec_rev_.get(), device()->data_role_.get(),
                                    ControlMessageType::GET_SOURCE_CAP);
        do {
          status = device()->FifoTransmit(req);
        } while (status == ZX_ERR_SHOULD_WAIT);
        if (status != ZX_OK) {
          zxlogf(ERROR, "FifoTransmit failed %d", status);
          return status;
        }
        break;
      }

      // Transitions
      SetState(pe_snk_ready);
      break;
    }
    case pe_snk_give_sink_cap:
    case pe_snk_hard_reset:
    case pe_snk_transition_to_default:
    case pe_db_cp_check_for_vbus: {
      // These states are currently not used because initialized_ is always true when Fusb302
      // boots up (meaning that the bootloader has already done the initial exchange). Implement
      // these when we need them.
      zxlogf(ERROR, "Unreachable state %u", state());
      return ZX_ERR_INTERNAL;
    }

    default: {
      zxlogf(ERROR, "Unrecognized state %u", state());
      return ZX_ERR_INTERNAL;
    }
  }
  return ZX_OK;
}

// For HW DRP State Machine Transitions, see FUSB302 Data Sheet Figure 11 (DRP Software Flow)
zx_status_t StateMachine::RunState(Event event, std::shared_ptr<PdMessage> message, bool entry) {
  zx_status_t status;
  switch (state()) {
    case disabled: {
      device()->is_cc_connected_ = false;
      if (event.cc()) {
        SetState((device()->power_role_.get() == sink) ? unattached_snk : unattached_src);
      }
      break;
    }
    case unattached_snk: {
      status = device()->Debounce();
      if (status != ZX_OK) {
        zxlogf(ERROR, "Debounce failed. %d", status);
        return status;
      }
      device()->is_cc_connected_ = true;

      // set msg header
      status = Switches1Reg::ReadFrom(device()->i2c_)
                   .set_power_role(device()->power_role_.get())
                   .set_data_role(device()->data_role_.get())
                   .set_spec_rev(device()->spec_rev_.get())
                   .WriteTo(device()->i2c_);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Write failed. %d", status);
        return status;
      }

      status = device()->SetPolarity(device()->polarity_.get());
      if (status != ZX_OK) {
        zxlogf(ERROR, "Set polarity failed. %d", status);
        return status;
      }
      status = device()->RxEnable(true);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Enable RX failed. %d", status);
        return status;
      }

      status = sink_policy_engine_.Init();
      if (status != ZX_OK) {
        zxlogf(ERROR, "Could not start Sink Policy Engine");
        break;
      }
      SetState(attached_snk);
      break;
    }
    case attached_snk: {
      sink_policy_engine_.Run(event, std::move(message));
      break;
    }
    case unattached_src:
    case attached_src: {
      // These states are currently not used because our current use cases only support SINK.
      zxlogf(ERROR, "Unreachable state %u", state());
      return ZX_ERR_INTERNAL;
    }
    default: {
      zxlogf(ERROR, "Unrecognized state %u", state());
      return ZX_ERR_INTERNAL;
    }
  }
  return ZX_OK;
}

}  // namespace fusb302
