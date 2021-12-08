// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fitx/result.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <mexec.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include <fbl/array.h>
#include <ktl/byte.h>
#include <ktl/variant.h>
#include <phys/handoff.h>

namespace {

fitx::result<fitx::failed> AppendGicConfig(MexecDataImage& image, const ktl::monostate& no_config) {
  return fitx::ok();
}

fitx::result<fitx::failed> AppendGicConfig(MexecDataImage& image,
                                           const dcfg_arm_gicv2_driver_t& config) {
  constexpr zbi_header_t kHeader = {
      .type = ZBI_TYPE_KERNEL_DRIVER,
      .extra = KDRV_ARM_GIC_V2,
  };
  if (auto result = image.Append(kHeader, zbitl::AsBytes(config)); result.is_error()) {
    printf("mexec: could not append GICv2 driver config: ");
    zbitl::PrintViewError(result.error_value());
    return fitx::failed();
  }
  return fitx::ok();
}

fitx::result<fitx::failed> AppendGicConfig(MexecDataImage& image,
                                           const dcfg_arm_gicv3_driver_t& config) {
  constexpr zbi_header_t kHeader = {
      .type = ZBI_TYPE_KERNEL_DRIVER,
      .extra = KDRV_ARM_GIC_V3,
  };
  if (auto result = image.Append(kHeader, zbitl::AsBytes(config)); result.is_error()) {
    printf("mexec: could not append GICv3 driver config: ");
    zbitl::PrintViewError(result.error_value());
    return fitx::failed();
  }
  return fitx::ok();
}

}  // namespace

fitx::result<fitx::failed> ArchAppendMexecDataFromHandoff(MexecDataImage& image,
                                                          PhysHandoff& handoff) {
  if (handoff.arch_handoff.amlogic_hdcp_driver) {
    constexpr zbi_header_t kHeader = {
        .type = ZBI_TYPE_KERNEL_DRIVER,
        .extra = KDRV_AMLOGIC_HDCP,
    };
    auto result =
        image.Append(kHeader, zbitl::AsBytes(handoff.arch_handoff.amlogic_hdcp_driver.value()));
    if (result.is_error()) {
      printf("mexec: could not append AMLogic HDCP driver config: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  if (handoff.arch_handoff.amlogic_rng_driver) {
    constexpr zbi_header_t kHeader = {
        .type = ZBI_TYPE_KERNEL_DRIVER,
        .extra = KDRV_AMLOGIC_RNG,
    };
    auto result =
        image.Append(kHeader, zbitl::AsBytes(handoff.arch_handoff.amlogic_rng_driver.value()));
    if (result.is_error()) {
      printf("mexec: could not append AMLogic RNG driver config: ");
    }
  }

  if (handoff.arch_handoff.generic_timer_driver) {
    constexpr zbi_header_t kHeader = {
        .type = ZBI_TYPE_KERNEL_DRIVER,
        .extra = KDRV_ARM_GENERIC_TIMER,
    };
    auto result =
        image.Append(kHeader, zbitl::AsBytes(handoff.arch_handoff.generic_timer_driver.value()));
    if (result.is_error()) {
      printf("mexec: could not append generic ARM timer driver config: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  auto append_gic_config = [&image](const auto& config) { return AppendGicConfig(image, config); };
  if (auto result = ktl::visit(append_gic_config, handoff.arch_handoff.gic_driver);
      result.is_error()) {
    return result;
  }

  if (handoff.arch_handoff.psci_driver) {
    constexpr zbi_header_t kHeader = {
        .type = ZBI_TYPE_KERNEL_DRIVER,
        .extra = KDRV_ARM_PSCI,
    };
    auto result = image.Append(kHeader, zbitl::AsBytes(handoff.arch_handoff.psci_driver.value()));
    if (result.is_error()) {
      printf("mexec: could not append PCI driver config: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  if (handoff.arch_handoff.generic_32bit_watchdog_driver) {
    constexpr zbi_header_t kHeader = {
        .type = ZBI_TYPE_KERNEL_DRIVER,
        .extra = KDRV_GENERIC_32BIT_WATCHDOG,
    };
    auto result = image.Append(
        kHeader, zbitl::AsBytes(handoff.arch_handoff.generic_32bit_watchdog_driver.value()));
    if (result.is_error()) {
      printf("mexec: could not append generic 32-bit watchdog driver config: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  return fitx::ok();
}
