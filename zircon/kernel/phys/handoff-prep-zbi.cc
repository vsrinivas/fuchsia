// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/items/cpu-topology.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <efi/types.h>
#include <ktl/algorithm.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/variant.h>
#include <phys/allocation.h>
#include <phys/handoff.h>
#include <phys/symbolize.h>
#include <phys/zbitl-allocation.h>

#include "handoff-entropy.h"
#include "handoff-prep.h"

#include <ktl/enforce.h>

void HandoffPrep::SummarizeMiscZbiItems(ktl::span<ktl::byte> zbi) {
  // TODO(fxbug.dev/84107): The data ZBI is still inspected by the kernel
  // proper until migrations are complete, so this communicates the physical
  // address during handoff.  This member should be removed as soon as the
  // kernel no longer examines the ZBI itself.
  handoff_->zbi = reinterpret_cast<uintptr_t>(zbi.data());

  // Allocate some pages to fill up with the ZBI items to save for mexec.
  // TODO(fxbug.dev/84107): Currently this is in scratch space and gets
  // copied into the handoff allocator when its final size is known.
  // Later, it will allocated with its own type and be handed off to
  // the kernel as a whole range of pages that can be turned into a VMO.
  fbl::AllocChecker ac;
  Allocation mexec_buffer =
      Allocation::New(ac, memalloc::Type::kPhysScratch, ZX_PAGE_SIZE, ZX_PAGE_SIZE);
  ZX_ASSERT_MSG(ac.check(), "cannot allocate mexec data page!");
  mexec_image_ = zbitl::Image(ktl::move(mexec_buffer));
  auto result = mexec_image_.clear();
  if (result.is_error()) {
    zbitl::PrintViewError(result.error_value());
  }
  ZX_ASSERT_MSG(result.is_ok(), "failed to initialize mexec data ZBI image");

  // Appends the appropriate UART config, as encoded in the hand-off, which is
  // given as variant of lib/uart driver types, each with methods to indicate
  // the ZBI item type and payload.
  auto append_uart_item = [this](const auto& uart) {
    const uint32_t kdrv_type = uart.extra();
    if (kdrv_type != 0) {  // Zero means the null driver.
      SaveForMexec({.type = ZBI_TYPE_KERNEL_DRIVER, .extra = kdrv_type},
                   zbitl::AsBytes(uart.config()));
    }
  };
  ktl::visit(append_uart_item, gBootOptions->serial);
  EntropyHandoff entropy;

  zbitl::View view(zbi);
  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    switch (header->type) {
      case ZBI_TYPE_HW_REBOOT_REASON:
        ZX_ASSERT(payload.size() >= sizeof(zbi_hw_reboot_reason_t));
        handoff_->reboot_reason = *reinterpret_cast<const zbi_hw_reboot_reason_t*>(payload.data());
        break;

      case ZBI_TYPE_NVRAM:
        ZX_ASSERT(payload.size() >= sizeof(zbi_nvram_t));
        handoff_->nvram = *reinterpret_cast<const zbi_nvram_t*>(payload.data());
        SaveForMexec(*header, payload);
        break;

      case ZBI_TYPE_PLATFORM_ID:
        ZX_ASSERT(payload.size() >= sizeof(zbi_platform_id_t));
        handoff_->platform_id = *reinterpret_cast<const zbi_platform_id_t*>(payload.data());
        SaveForMexec(*header, payload);
        break;

      case ZBI_TYPE_MEM_CONFIG: {
        // Pass the original incoming data on for mexec verbatim.
        SaveForMexec(*header, payload);

        // TODO(fxbug.dev/84107): Hand off the incoming ZBI item data directly
        // rather than using normalized data from memalloc::Pool so that the
        // kernel's ingestion of RAM vs RESERVED regions is unperturbed.
        // Later this will be replaced by proper memory handoff.

        const ktl::span mem_config{
            reinterpret_cast<const zbi_mem_range_t*>(payload.data()),
            payload.size_bytes() / sizeof(zbi_mem_range_t),
        };

        ktl::optional<zbi_mem_range_t> test_ram_reserve;
        if (gBootOptions->test_ram_reserve && gBootOptions->test_ram_reserve->paddr) {
          test_ram_reserve = {
              .paddr = *gBootOptions->test_ram_reserve->paddr,
              .length = gBootOptions->test_ram_reserve->size,
              .type = ZBI_MEM_RANGE_RESERVED,
          };
        }

        ktl::span handoff_mem_config =
            New(handoff()->mem_config, ac, mem_config.size() + (test_ram_reserve ? 1 : 0));
        ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for memory handoff",
                      mem_config.size_bytes());

        ktl::copy(mem_config.begin(), mem_config.end(), handoff_mem_config.begin());
        if (test_ram_reserve) {
          // TODO(mcgrathr): Note this will persist into the mexec handoff from
          // the kernel and be elided from the next kernel.  But that will be
          // fixed shortly when mexec handoff is handled directly here instead.
          handoff_mem_config.back() = *test_ram_reserve;
        }
        break;
      }

      case ZBI_TYPE_CPU_CONFIG:
      case ZBI_TYPE_CPU_TOPOLOGY:
        // Normalize either item type into zbi_topology_node_t[] for handoff.
        if (auto table = zbitl::CpuTopologyTable::FromPayload(header->type, payload);
            table.is_ok()) {
          ktl::span handoff_table = New(handoff()->cpu_topology, ac, table->size());
          ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for CPU topology handoff",
                        table->size_bytes());
          ZX_DEBUG_ASSERT(handoff_table.size() == table->size());
          ktl::copy(table->begin(), table->end(), handoff_table.begin());
        } else {
          printf("%s: NOTE: ignored invalid CPU topology payload: %.*s\n", ProgramName(),
                 static_cast<int>(table.error_value().size()), table.error_value().data());
        }
        SaveForMexec(*header, payload);
        break;

      case ZBI_TYPE_CRASHLOG: {
        ktl::span buffer = New(handoff_->crashlog, ac, payload.size());
        ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for crash log", payload.size());
        memcpy(buffer.data(), payload.data(), payload.size_bytes());
        // The crashlog is propagated separately by the kernel.
        break;
      }

      case ZBI_TYPE_SECURE_ENTROPY:
        entropy.AddEntropy(it->payload);
        ZX_ASSERT(it.view().EditHeader(it, {.type = ZBI_TYPE_DISCARD}).is_ok());
        ZX_ASSERT(it->header->type == ZBI_TYPE_DISCARD);
#if ZX_DEBUG_ASSERT_IMPLEMENTED
        // Verify that the payload contents have been zeroed.
        for (auto b : it->payload) {
          ZX_DEBUG_ASSERT(static_cast<char>(b) == 0);
        }
#endif
        break;
      case ZBI_TYPE_ACPI_RSDP:
        ZX_ASSERT(payload.size() >= sizeof(uint64_t));
        handoff()->acpi_rsdp = *reinterpret_cast<const uint64_t*>(payload.data());
        SaveForMexec(*header, payload);
        break;
      case ZBI_TYPE_SMBIOS:
        ZX_ASSERT(payload.size() >= sizeof(uint64_t));
        handoff()->smbios_phys = *reinterpret_cast<const uint64_t*>(payload.data());
        SaveForMexec(*header, payload);
        break;

      case ZBI_TYPE_EFI_MEMORY_ATTRIBUTES_TABLE: {
        ktl::span handoff_table = New(handoff()->efi_memory_attributes, ac, payload.size());
        ZX_ASSERT_MSG(ac.check(), "Cannot allocate %zu bytes for EFI memory attributes",
                      payload.size());
        ktl::copy(payload.begin(), payload.end(), handoff_table.begin());

        SaveForMexec(*header, payload);
        break;
      }

      // Default assumption is that the type is architecture-specific.
      default:
        ArchSummarizeMiscZbiItem(*header, payload);
        break;
    }
  }

  // Clears the contents of 'entropy_mixin' when consumed for security reasons.
  entropy.AddEntropy(*const_cast<BootOptions*>(gBootOptions));

  // Depending on certain boot options, failure to meet entropy requirements may cause
  // the program to abort after this point.
  handoff_->entropy_pool = ktl::move(entropy).Take(*gBootOptions);

  // At this point we should have full confidence that the ZBI is properly
  // formatted.
  ZX_ASSERT(view.take_error().is_ok());

  // Copy mexec data into handoff temporary space.
  // TODO(fxbug.dev/84107): Later this won't be required since we'll pass
  // the contents of mexec_image_ to the kernel in the handoff by address.
  ktl::span handoff_mexec = New(handoff_->mexec_data, ac, mexec_image_.size_bytes());
  ZX_ASSERT(ac.check());
  memcpy(handoff_mexec.data(), mexec_image_.storage().get(), handoff_mexec.size_bytes());
}

void HandoffPrep::SaveForMexec(const zbi_header_t& header, ktl::span<const ktl::byte> payload) {
  auto result = mexec_image_.Append(header, payload);
  if (result.is_error()) {
    printf("%s: ERROR: failed to append item of %zu bytes to mexec image: ", ProgramName(),
           payload.size_bytes());
    zbitl::PrintViewError(result.error_value());
  }
  // Don't make it fatal in production if there's too much to fit.
  ZX_DEBUG_ASSERT(result.is_ok());
}
