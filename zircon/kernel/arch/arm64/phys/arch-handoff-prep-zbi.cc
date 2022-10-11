// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <zircon/assert.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/variant.h>
#include <phys/arch/arch-handoff.h>
#include <phys/handoff.h>

#include "handoff-prep.h"

#include <ktl/enforce.h>

void HandoffPrep::ArchSummarizeMiscZbiItem(const zbi_header_t& header,
                                           ktl::span<const ktl::byte> payload) {
  ZX_DEBUG_ASSERT(handoff_);
  ArchPhysHandoff& arch_handoff = handoff_->arch_handoff;

  switch (header.type) {
    case ZBI_TYPE_KERNEL_DRIVER: {
      switch (header.extra) {
        // TODO(fxbug.dev/87958): Move me to userspace.
        case ZBI_KERNEL_DRIVER_AMLOGIC_HDCP:
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_amlogic_hdcp_driver_t));
          arch_handoff.amlogic_hdcp_driver =
              *reinterpret_cast<const zbi_dcfg_amlogic_hdcp_driver_t*>(payload.data());
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_AMLOGIC_RNG:
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_amlogic_rng_driver_t));
          arch_handoff.amlogic_rng_driver =
              *reinterpret_cast<const zbi_dcfg_amlogic_rng_driver_t*>(payload.data());
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_ARM_GENERIC_TIMER:
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_arm_generic_timer_driver_t));
          arch_handoff.generic_timer_driver =
              *reinterpret_cast<const zbi_dcfg_arm_generic_timer_driver_t*>(payload.data());
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_ARM_GIC_V2:
          // Defer to the newer hardware: v3 configs win out over v2.
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_arm_gic_v2_driver_t));
          if (!ktl::holds_alternative<zbi_dcfg_arm_gic_v3_driver_t>(arch_handoff.gic_driver)) {
            arch_handoff.gic_driver =
                *reinterpret_cast<const zbi_dcfg_arm_gic_v2_driver_t*>(payload.data());
          }
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_ARM_GIC_V3:
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_arm_gic_v3_driver_t));
          arch_handoff.gic_driver =
              *reinterpret_cast<const zbi_dcfg_arm_gic_v3_driver_t*>(payload.data());
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_ARM_PSCI:
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_arm_psci_driver_t));
          arch_handoff.psci_driver =
              *reinterpret_cast<const zbi_dcfg_arm_psci_driver_t*>(payload.data());
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_GENERIC32_WATCHDOG:
          ZX_ASSERT(payload.size() >= sizeof(zbi_dcfg_generic32_watchdog_t));
          arch_handoff.generic32_watchdog_driver =
              *reinterpret_cast<const zbi_dcfg_generic32_watchdog_t*>(payload.data());
          SaveForMexec(header, payload);
          break;
        case ZBI_KERNEL_DRIVER_AS370_POWER:
          ZX_ASSERT(payload.size() == 0);
          arch_handoff.as370_power_driver = true;
          break;
        case ZBI_KERNEL_DRIVER_MOTMOT_POWER:
          ZX_ASSERT(payload.size() == 0);
          arch_handoff.motmot_power_driver = true;
          break;
      }
      break;
    }
  }
}
