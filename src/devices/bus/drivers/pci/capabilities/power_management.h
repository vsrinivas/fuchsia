// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_POWER_MANAGEMENT_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_POWER_MANAGEMENT_H_

#include <lib/zx/status.h>

#include <hwreg/bitfields.h>

#include "src/devices/bus/drivers/pci/capabilities.h"
#include "src/devices/bus/drivers/pci/config.h"

namespace pci {

constexpr uint8_t kPmcVersion = 0b011;

// The Power Management capability is detailed in the PCIe Base Spec chapter 7.5.2.1: Power
// Management Capability.

// All fields in this register are read only.
struct PmcReg {
  uint16_t value;
  // Hardwired to 011 in this specification version.
  DEF_SUBFIELD(value, 2, 0, version);
  // Hardwired to 0 in PCIe.
  DEF_SUBBIT(value, 3, pme_clock);
  // Set if no delay is needed following a transition to state D0.
  DEF_SUBBIT(value, 4, immediate_readiness_on_return_to_d0);
  // Set if the device has a special initialization sequence following a D0 transition
  // and cannot be managed entirely by the bus driver.
  DEF_SUBBIT(value, 5, dsi);
  // Details the auxiliary current required. Hardwired to 0 if the Data register is implemented.
  // 111 -> 375 mA
  // 110 -> 320 mA
  // 101 -> 270 mA
  // 100 -> 220 mA
  // 011 -> 160 mA
  // 010 -> 100 mA
  // 001 -> 55 mA
  // 000 -> 0 (self powered)
  DEF_SUBFIELD(value, 8, 6, aux_current);
  // Set if D1 is supported.
  DEF_SUBBIT(value, 9, d1_support);
  // Set if D2 is supported.
  DEF_SUBBIT(value, 10, d2_support);
  // A bitmask corresponding to whether a given power state can generate a PME#.
  // In order from 15:11, D3cold D3hot D2 D1 D0
  DEF_SUBFIELD(value, 15, 11, pme_support);
};

struct PmcsrReg {
  uint16_t value;
  // RW. The current power state from 0 to D3hot.
  DEF_SUBFIELD(value, 1, 0, power_state);
  // RO. Set if function state is preserved after state transition from D3hot to
  //     D0. Otherwise, the device state is undefined.
  DEF_SUBBIT(value, 3, no_soft_reset);
  // RW/RWS. Set if the function is permitted to generate a PME.
  DEF_SUBBIT(value, 8, pme_en);
  // RW. Controls which data to route to the Data register.
  DEF_SUBFIELD(value, 12, 9, data_select);
  // RO. Scaling factor for the data register.
  DEF_SUBFIELD(value, 14, 13, data_scale);
  // RW. Set if the function would normally generate a PME signal.
  DEF_SUBBIT(value, 15, pme_Status);
};

class PowerManagementCapability : public Capability {
 public:
  // The minimum amount of time needed to wait following a transition from one
  // state to another. In the case of invalid transitions (Dx -> Dy (x < y))
  // they are just zero because they will not be used. The exception is D0 which
  // can transition to any other state.
  // PCIe Base Spec rev 4 5.9.1 State Transition Recovery Time Requirements
  // clang-format off
  static constexpr zx::duration kStateRecoveryTime[4][4] = {
      /*D0*/ {/*D0*/ zx::usec(0),   /*D1*/ zx::usec(0), /*D2*/ zx::usec(200), /*D3hot*/ zx::msec(10)},
      /*D1*/ {/*D0*/ zx::usec(0),   /*D1*/ zx::usec(0), /*D2*/ zx::usec(200), /*D3hot*/ zx::msec(10)},
      /*D2*/ {/*D0*/ zx::usec(200), /*D1*/ zx::usec(0), /*D2*/ zx::usec(0),   /*D3hot*/ zx::msec(10)},
      /*D3*/ {/*D0*/ zx::msec(10),  /*D1*/ zx::usec(0), /*D2*/ zx::usec(0),   /*D3hot*/ zx::usec(0)},
  };
  // clang-format on

  // Power states as defined in the spec. D3cold is considered "powered off" and
  // is not reflected in power states.
  enum PowerState : uint8_t {
    D0 = 0,
    D1 = 1,
    D2 = 2,
    D3 = 3,  // D3hot
  };

  PowerManagementCapability(const Config& cfg, uint8_t base)
      : Capability(static_cast<uint8_t>(Capability::Id::kPciPowerManagement), base),
        pmc_(PciReg16(static_cast<uint16_t>(base + 0x2))),
        pmcsr_(PciReg16(static_cast<uint16_t>(base + 0x4))),
        data_(PciReg8(static_cast<uint16_t>(base + 0x7))) {}

  void WaitForTransitionRecovery(PowerState old_state, PowerState new_state);
  PowerState GetPowerState(const pci::Config& cfg);
  void SetPowerState(const pci::Config& cfg, PowerState new_state);

  PciReg16 pmc() { return pmc_; }
  PciReg16 pmcsr() { return pmcsr_; }

 private:
  const PciReg16 pmc_;     // Power Management Capabilities
  const PciReg16 pmcsr_;   // Power Management Control/Status Register
  __UNUSED PciReg8 data_;  // Data
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CAPABILITIES_POWER_MANAGEMENT_H_
