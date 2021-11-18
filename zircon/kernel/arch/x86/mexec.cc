// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fitx/result.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <mexec.h>
#include <stdio.h>

#include <fbl/array.h>
#include <ktl/byte.h>
#include <phys/handoff.h>

fitx::result<fitx::failed> ArchAppendMexecDataFromHandoff(MexecDataImage& image,
                                                          PhysHandoff& handoff) {
  if (handoff.arch_handoff.acpi_rsdp) {
    auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_ACPI_RSDP},
                               zbitl::AsBytes(handoff.arch_handoff.acpi_rsdp.value()));
    if (result.is_error()) {
      printf("mexec: could not append ACPI RSDP address: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  if (handoff.arch_handoff.efi_system_table) {
    auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_EFI_SYSTEM_TABLE},
                               zbitl::AsBytes(handoff.arch_handoff.efi_system_table.value()));
    if (result.is_error()) {
      printf("mexec: could not append EFI system table address: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  if (handoff.arch_handoff.framebuffer) {
    auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_FRAMEBUFFER},
                               zbitl::AsBytes(handoff.arch_handoff.framebuffer.value()));
    if (result.is_error()) {
      printf("mexec: could not append framebuffer data: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  if (handoff.arch_handoff.smbios) {
    auto result = image.Append(zbi_header_t{.type = ZBI_TYPE_SMBIOS},
                               zbitl::AsBytes(handoff.arch_handoff.smbios.value()));
    if (result.is_error()) {
      printf("mexec: could not append SMBIOS pointer: ");
      zbitl::PrintViewError(result.error_value());
      return fitx::failed();
    }
  }

  return fitx::ok();
}
