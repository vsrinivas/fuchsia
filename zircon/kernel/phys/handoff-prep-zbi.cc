// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zbitl/items/cpu-topology.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/algorithm.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <phys/handoff.h>
#include <phys/symbolize.h>

#include "handoff-prep.h"

void HandoffPrep::SummarizeMiscZbiItems(ktl::span<const ktl::byte> zbi) {
  // TODO(fxbug.dev/84107): The data ZBI is still inspected by the kernel
  // proper until migrations are complete, so this communicates the physical
  // address during handoff.  This member should be removed as soon as the
  // kernel no longer examines the ZBI itself.
  handoff_->zbi = reinterpret_cast<uintptr_t>(zbi.data());

  zbitl::View view(zbi);
  for (auto [header, payload] : view) {
    switch (header->type) {
      case ZBI_TYPE_HW_REBOOT_REASON:
        ZX_ASSERT(payload.size() >= sizeof(zbi_hw_reboot_reason_t));
        handoff_->reboot_reason = *reinterpret_cast<const zbi_hw_reboot_reason_t*>(payload.data());
        break;

      case ZBI_TYPE_NVRAM:
        ZX_ASSERT(payload.size() >= sizeof(zbi_nvram_t));
        handoff_->nvram = *reinterpret_cast<const zbi_nvram_t*>(payload.data());
        break;

      case ZBI_TYPE_PLATFORM_ID:
        ZX_ASSERT(payload.size() >= sizeof(zbi_platform_id_t));
        handoff_->platform_id = *reinterpret_cast<const zbi_platform_id_t*>(payload.data());
        break;

      case ZBI_TYPE_MEM_CONFIG: {
        // TODO(fxbug.dev/84107): Hand off the incoming ZBI item data directly
        // rather than using normalized data from memalloc::Pool so that the
        // kernel's ingestion of RAM vs RESERVED regions is unperturbed.
        // Later this will be replaced by proper memory handoff.
        const ktl::span mem_config{
            reinterpret_cast<const zbi_mem_range_t*>(payload.data()),
            payload.size_bytes() / sizeof(zbi_mem_range_t),
        };
        fbl::AllocChecker ac;
        ktl::span handoff_mem_config = New(handoff()->mem_config, ac, mem_config.size());
        ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for memory handoff",
                      mem_config.size_bytes());
        ktl::copy(mem_config.begin(), mem_config.end(), handoff_mem_config.begin());
        break;
      }

      case ZBI_TYPE_CPU_CONFIG:
      case ZBI_TYPE_CPU_TOPOLOGY:
        // Normalize either item type into zbi_topology_node_t[] for handoff.
        if (auto table = zbitl::CpuTopologyTable::FromPayload(header->type, payload);
            table.is_ok()) {
          fbl::AllocChecker ac;
          ktl::span handoff_table = New(handoff()->cpu_topology, ac, table->size());
          ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for CPU topology handoff",
                        table->size_bytes());
          ZX_DEBUG_ASSERT(handoff_table.size() == table->size());
          ktl::copy(table->begin(), table->end(), handoff_table.begin());
        } else {
          printf("%s: NOTE: ignored invalid CPU topology payload: %.*s\n", Symbolize::kProgramName_,
                 static_cast<int>(table.error_value().size()), table.error_value().data());
        }
        break;

      case ZBI_TYPE_CRASHLOG: {
        fbl::AllocChecker ac;
        ktl::span buffer = New(handoff_->crashlog, ac, payload.size());
        ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for crash log", payload.size());
        memcpy(buffer.data(), payload.data(), payload.size_bytes());
        break;
      }

      // Default assumption is that the type is architecture-specific.
      default:
        ArchSummarizeMiscZbiItem(*header, payload);
        break;
    }
  }

  // At this point we should have full confidence that the ZBI is properly
  // formatted.
  ZX_ASSERT(view.take_error().is_ok());
}
