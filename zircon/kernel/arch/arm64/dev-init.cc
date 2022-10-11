// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/all.h>
#include <lib/uart/null.h>

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
#include <dev/uart/amlogic_s905/init.h>
#include <dev/uart/dw8250/init.h>
#include <dev/uart/motmot/init.h>
#include <dev/uart/pl011/init.h>
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

// Overloads for early UART initialization below.
void UartInitEarly(uint32_t extra, const uart::null::Driver::config_type& config) {}

void UartInitEarly(uint32_t extra, const zbi_dcfg_simple_t& config) {
  switch (extra) {
    case ZBI_KERNEL_DRIVER_AMLOGIC_UART:
      AmlogicS905UartInitEarly(config);
      break;
    case ZBI_KERNEL_DRIVER_DW8250_UART:
      Dw8250UartInitEarly(config);
      break;
    case ZBI_KERNEL_DRIVER_MOTMOT_UART:
      MotmotUartInitEarly(config);
      break;
    case ZBI_KERNEL_DRIVER_PL011_UART:
      Pl011UartInitEarly(config);
      break;
  }
}

void UartInitLate(uint32_t extra) {
  switch (extra) {
    case ZBI_KERNEL_DRIVER_AMLOGIC_UART:
      AmlogicS905UartInitLate();
      break;
    case ZBI_KERNEL_DRIVER_DW8250_UART:
      Dw8250UartInitLate();
      break;
    case ZBI_KERNEL_DRIVER_MOTMOT_UART:
      MotmotUartInitLate();
      break;
    case ZBI_KERNEL_DRIVER_PL011_UART:
      Pl011UartInitLate();
      break;
  }
}

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

void ArchUartDriverHandoffEarly(const uart::all::Driver& serial) {
  ktl::visit([](const auto& uart) { UartInitEarly(uart.extra(), uart.config()); }, serial);
}

void ArchUartDriverHandoffLate(const uart::all::Driver& serial) {
  ktl::visit([](const auto& uart) { UartInitLate(uart.extra()); }, serial);
}
