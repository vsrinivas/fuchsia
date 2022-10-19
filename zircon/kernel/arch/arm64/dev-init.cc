// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/hdcp/amlogic_s912/init.h>
#include <dev/hw_rng/amlogic_rng/init.h>
#include <dev/hw_watchdog/generic32/init.h>
#include <dev/init.h>
#include <dev/interrupt/arm_gicv2_init.h>
#include <dev/interrupt/arm_gicv3_init.h>
#include <dev/power/as370/init.h>
#include <dev/power/motmot/init.h>
#include <dev/psci.h>
#include <dev/timer/arm_generic.h>
#include <ktl/type_traits.h>
#include <ktl/variant.h>
#include <phys/arch/arch-handoff.h>

#include <ktl/enforce.h>

namespace {

// Overloads to satisfy the degenerate 'no config present' case in
// `ktl::visit(..., arch_handoff.gic_driver)` below. Related overloads defined
// in <dev/interrupt/arm_gicv{2,3}_init.h>.
void ArmGicInitEarly(const ktl::monostate& no_config) {}
void ArmGicInitLate(const ktl::monostate& no_config) {}

}  // namespace

void ArchDriverHandoffEarly(const ArchPhysHandoff& arch_handoff) {
  // Configure the GIC first so that the remaining drivers can freely register
  // interrupt handlers.
  ktl::visit([](const auto& config) { ArmGicInitEarly(config); }, arch_handoff.gic_driver);

  if (arch_handoff.generic32_watchdog_driver) {
    Generic32BitWatchdogEarlyInit(arch_handoff.generic32_watchdog_driver.value());
  }

  if (arch_handoff.generic_timer_driver) {
    ArmGenericTimerInit(arch_handoff.generic_timer_driver.value());
  }

  if (arch_handoff.psci_driver) {
    PsciInit(arch_handoff.psci_driver.value());
  }

  if (arch_handoff.as370_power_driver) {
    as370_power_init_early();
  }

  if (arch_handoff.motmot_power_driver) {
    motmot_power_init_early();
  }
}

void ArchDriverHandoffLate(const ArchPhysHandoff& arch_handoff) {
  // First, as above.
  ktl::visit([](const auto& config) { ArmGicInitLate(config); }, arch_handoff.gic_driver);

  if (arch_handoff.amlogic_hdcp_driver) {
    AmlogicS912HdcpInit(arch_handoff.amlogic_hdcp_driver.value());
  }

  if (arch_handoff.amlogic_rng_driver) {
    AmlogicRngInit(arch_handoff.amlogic_rng_driver.value());
  }

  if (arch_handoff.generic32_watchdog_driver) {
    Generic32BitWatchdogLateInit();
  }
}
