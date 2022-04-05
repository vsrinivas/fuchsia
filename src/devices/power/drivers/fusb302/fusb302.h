// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_FUSB302_FUSB302_H_
#define SRC_DEVICES_POWER_DRIVERS_FUSB302_FUSB302_H_

#include <fidl/fuchsia.hardware.power/cpp/wire.h>
#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <lib/zx/interrupt.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>

#include "src/devices/power/drivers/fusb302/inspectable-types.h"
#include "src/devices/power/drivers/fusb302/registers.h"
#include "src/devices/power/drivers/fusb302/state-machine-base.h"

namespace fusb302 {

using usb::pd::SpecRev;
using PowerDataObject = usb::pd::DataPdMessage::PowerDataObject;

enum PortPacketType : uint64_t {
  kInterrupt = 0x1,  // Check Interrupt registers, set events, and run State Machine
  kTimer = 0x2,      // Just run the State Machine (which will deal with timers)
};

int constexpr kChargeInputDefaultCur = 6000;
int constexpr kChargeInputDefaultVol = 12000;

class SinkPolicyEngine;
class StateMachine;
class Fusb302;
using DeviceType = ddk::Device<Fusb302, ddk::Messageable<fuchsia_hardware_power::Source>::Mixin>;

// Sink Policy Engine States. States for SinkPolicyEngine.
enum SinkPolicyEngineStates : uint32_t {
  pe_snk_startup,
  pe_snk_discovery,
  pe_snk_wait_for_capabilities,
  pe_snk_evaluate_capability,
  pe_snk_select_capability,
  pe_snk_transition_sink,
  pe_snk_ready,
  pe_snk_get_source_cap,
  pe_snk_give_sink_cap,
  pe_snk_hard_reset,
  pe_snk_transition_to_default,
  pe_db_cp_check_for_vbus,
};

// SinkPolicyEngine: Sink Policy Engine state machine for USB-PD Protocol.
class SinkPolicyEngine : public StateMachineBase<SinkPolicyEngineStates, Fusb302> {
 public:
  SinkPolicyEngine(Fusb302* device, bool initialized, inspect::Node& inspect_root)
      : StateMachineBase(device, pe_snk_startup, inspect_root, "SinkPolicyEngine"),
        initialized_(initialized) {}
  ~SinkPolicyEngine() = default;

  zx_status_t Init();

 private:
  zx_status_t RunState(Event event, std::shared_ptr<PdMessage> message, bool entry) override;
  uint8_t FindPdo(uint32_t max_voltage_mV, uint32_t max_current_mA);

  InspectablePdoArray source_capabilities_ = InspectablePdoArray(inspect(), "Capabilities");
  InspectableUint<uint8_t> curr_object_position_ =
      InspectableUint<uint8_t>(inspect(), "CurrentCapabilityIndex", UINT8_MAX);
  InspectableUint<uint64_t> requested_max_curr_mA_ =
      InspectableUint<uint64_t>(inspect(), "RequestedMaxCurrent_mA", kChargeInputDefaultCur);
  InspectableUint<uint64_t> requested_max_volt_mV_ =
      InspectableUint<uint64_t>(inspect(), "RequestedMaxVoltage_mV", kChargeInputDefaultVol);

  // initialized_: Whether or not initialization happened in the bootloader and whether or not we've
  // corrected for it. Currently hard coded to true because the only use of it in Fuchsia should be
  // after the bootloader has set up Fusb302.
  bool initialized_;

  // State Machine Timers
  zx::timer sink_wait_cap_timer_;
};

// HW DRP (dual role port) Toggling States. States for StateMachine.
enum HwDrpStates : uint32_t {
  disabled,        // low power mode looking for an attach
  unattached_snk,  // Host software enables FUSB302B pull-downs and measure block to detect attach
  attached_snk,    // Host software uses FUSB302B comparators and DAC to determine attach
                   // orientation and port type
  unattached_src,  // Host software enables FUSB302 pull-ups and measure block to detect attach
  attached_src,    // Host software configures FUSB302B based on insertion orientation and enables
                   // VBUS and VCONN
};

// StateMachine: HW DRP (Dual Role Port) state machine that configures the HW correctly based on
// which state is found and runs the correct policy engine state machine when in the correct state.
class StateMachine : public StateMachineBase<HwDrpStates, Fusb302> {
 public:
  StateMachine(Fusb302* device, bool initialized, inspect::Node& inspect_root)
      : StateMachineBase(device, disabled, inspect_root, "StateMachine"),
        sink_policy_engine_(device, initialized, inspect_root) {}
  ~StateMachine() = default;

  void Restart() { SetState(disabled); }

 private:
  zx_status_t RunState(Event event, std::shared_ptr<PdMessage> message, bool entry) override;

  // Sink Policy Engine State Machine. Should be run when in attached_snk mode.
  SinkPolicyEngine sink_policy_engine_;
  // Source Policy Engine State Machine. Should be run when in attached_src mode. To be implemented
  // when needed.
};

// Fusb302: Device that keeps track of the state of the HW, services FIDL requests, and runs the IRQ
// thread, which in turn runs StateMachine when called on.
class Fusb302 : public DeviceType {
 public:
  Fusb302(zx_device_t* parent, const ddk::I2cProtocolClient& i2c, zx::interrupt irq)
      : DeviceType(parent), i2c_(i2c), irq_(std::move(irq)) {}
  ~Fusb302() override {
    irq_.destroy();
    if (is_thread_running_) {
      thrd_join(irq_thread_, nullptr);
      is_thread_running_ = false;
    }
  }

  static zx_status_t Create(void* context, zx_device_t* parent);

  void DdkRelease() { delete this; }

  // TODO (rdzhuang): change power FIDL to supply required values in SourceInfo
  void GetPowerInfo(GetPowerInfoRequestView request,
                    GetPowerInfoCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  void GetStateChangeEvent(GetStateChangeEventRequestView request,
                           GetStateChangeEventCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }
  void GetBatteryInfo(GetBatteryInfoRequestView request,
                      GetBatteryInfoCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }

 private:
  friend class Fusb302Test;
  friend class StateMachine;
  friend class SinkPolicyEngine;

  // Initialization Functions and Variables
  zx_status_t Init();
  zx_status_t InitInspect();
  zx_status_t InitHw();
  zx_status_t IrqThread();
  zx::status<Event> GetInterrupt();

  ddk::I2cProtocolClient i2c_;
  zx::interrupt irq_;
  zx::port port_;
  std::atomic_bool is_thread_running_ = false;
  thrd_t irq_thread_;

  // Inspect Variables
  inspect::Inspector inspect_;
  inspect::Node inspect_device_id_ = inspect_.GetRoot().CreateChild("DeviceId");
  inspect::Node inspect_hw_drp_ = inspect_.GetRoot().CreateChild("HardwareDRP");

  // state_machine_: HW DRP (Dual Role Port) state machine which will run the policy engine state
  // machines when in the correct attached states.
  StateMachine state_machine_ = StateMachine(this, /* initialized */ true, inspect_.GetRoot());
  uint8_t message_id_ = 0;

  // Hardware DRP Helper Functions and Variables
  zx_status_t SetPolarity(Polarity polarity);
  zx_status_t SetCC(DataRole mode);
  zx_status_t RxEnable(bool enable);

  zx_status_t GetCC(uint8_t* cc1, uint8_t* cc2);
  uint8_t MeasureCC(Polarity polarity);
  zx_status_t Debounce();

  zx_status_t FifoTransmit(const PdMessage& message);
  zx::status<PdMessage> FifoReceive();

  bool is_cc_connected_ = false;
  InspectableBool<PowerRole> power_role_ =
      InspectableBool<PowerRole>(&inspect_hw_drp_, "PowerRole", sink);
  InspectableUint<DataRole> data_role_ =
      InspectableUint<DataRole>(&inspect_hw_drp_, "DataRole", UFP);
  InspectableUint<SpecRev> spec_rev_ =
      InspectableUint<SpecRev>(&inspect_hw_drp_, "SpecRev", SpecRev::kRev2);
  InspectableBool<Polarity> polarity_ =
      InspectableBool<Polarity>(&inspect_hw_drp_, "Polarity", CC1);
  enum TxState : uint8_t {
    busy,
    failed,
    success,
  };
  InspectableUint<TxState> tx_state_ =
      InspectableUint<TxState>(&inspect_hw_drp_, "TxState", success);
};

}  // namespace fusb302

#endif  // SRC_DEVICES_POWER_DRIVERS_FUSB302_FUSB302_H_
