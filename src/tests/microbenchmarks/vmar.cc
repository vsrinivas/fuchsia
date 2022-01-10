// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measures the time taken to perform zx_vmar_protect over multiple mappings inside a vmar. This is
// distinct from just causing there to be multiple protection regions inside a single mapping. The
// protection is performed on a subset of |protect_mappings| inside of |total_mappings| to evaluate
// the lookup and iteration of mappings in the vmar tree. If |toggle_protect| is true then the
// zx_vmar_protect calls will continuously alternate permissions, preventing any short circuiting.
//
// The mappings themselves will deliberately not get populated in the mmu so that this measures just
// the vmar hierarchy, and not the arch specific mmu code.
bool VmarMultiMappingsProtect(perftest::RepeatState* state, bool toggle_protect,
                              uint32_t total_mappings, uint32_t protect_mappings) {
  // Create a VMAR to hold all the mappings.
  const uint32_t page_size = zx_system_get_page_size();
  const size_t vmar_size = total_mappings * page_size;
  zx::vmar vmar;
  zx_vaddr_t addr = 0;
  ASSERT_OK(zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, vmar_size, &vmar,
      &addr));

  // Create a VMO to map in that is twice as large as the VMAR so that every second page can be
  // mapped. Mapping in every second page prevents mappings from being internally merged.
  const size_t vmo_size = vmar_size * 2;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));

  // Map in every second page.
  for (uint32_t i = 0; i < total_mappings; i++) {
    zx_vaddr_t map_addr;
    const size_t vmar_offset = i * page_size;
    const uint64_t vmo_offset = i * 2 * page_size;
    ASSERT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, vmar_offset, vmo,
                       vmo_offset, page_size, &map_addr));
  }

  // Skew the protect address to be in the middle of the mapping so that some kind of interesting
  // tree walk has to happen. Ideally we would use many different offsets, but this is a lot of
  // additional permutations without much expected benefit.
  const zx_vaddr_t protect_addr = addr + ((total_mappings - protect_mappings) / 2 * page_size);
  const size_t protect_size = protect_mappings * page_size;
  // For the case of |toggle_protect| we will alternate the write permissions of the mapping.
  bool protect_write = true;
  while (state->KeepRunning()) {
    ASSERT_OK(vmar.protect(ZX_VM_PERM_READ | (protect_write ? ZX_VM_PERM_WRITE : 0), protect_addr,
                           protect_size));
    if (toggle_protect) {
      protect_write = !protect_write;
    }
  }
  vmar.destroy();
  return true;
}

// Measures the time taken to decommit pages from a VMO via the VMAR mappings. |commit| controls
// whether pages are committed to the VMO, and hence whether the decommit step performs any true
// work, with |num_mappings| * |pages_per_mapping| being the total number of pages committed and
// decommitted.
//
// This is functionally equivalent to performing decommit directly on the VMO, however the VMAR
// lookup and walking adds overhead that we want to measure.
bool VmarDecommit(perftest::RepeatState* state, bool commit, uint32_t num_mappings,
                  uint32_t pages_per_mapping) {
  // Create a VMAR to hold all the mappings.
  const uint32_t page_size = zx_system_get_page_size();
  const size_t vmar_size = num_mappings * pages_per_mapping * page_size;
  zx::vmar vmar;
  zx_vaddr_t addr = 0;
  ASSERT_OK(zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, vmar_size, &vmar,
      &addr));

  // Create a VMO to map in that is twice as large as the VMAR so that every second range can be
  // mapped. Mapping in every second range prevents mappings from being internally merged.
  const size_t vmo_size = vmar_size * 2;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &vmo));

  // Map in every second range.
  const size_t mapping_size = pages_per_mapping * page_size;
  for (uint32_t i = 0; i < num_mappings; i++) {
    zx_vaddr_t map_addr;
    const size_t vmar_offset = i * mapping_size;
    const uint64_t vmo_offset = i * 2 * mapping_size;
    ASSERT_OK(vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC, vmar_offset, vmo,
                       vmo_offset, mapping_size, &map_addr));
  }

  if (commit) {
    state->DeclareStep("Commit");
  }
  state->DeclareStep("Decommit");

  while (state->KeepRunning()) {
    if (commit) {
      ASSERT_OK(vmar.op_range(ZX_VMAR_OP_COMMIT, addr, vmar_size, nullptr, 0));
      state->NextStep();
    }
    ASSERT_OK(vmar.op_range(ZX_VMAR_OP_DECOMMIT, addr, vmar_size, nullptr, 0));
  }
  vmar.destroy();
  return true;
}

void RegisterTests() {
  for (unsigned total : {1, 16, 128}) {
    for (unsigned protect : {1, 16, 128}) {
      if (protect > total) {
        continue;
      }
      for (bool toggle : {true, false}) {
        auto protect_name = fbl::StringPrintf("Vmar/Protect%s/%uMappings/%uProtect",
                                              toggle ? "Toggle" : "Same", total, protect);
        perftest::RegisterTest(protect_name.c_str(), VmarMultiMappingsProtect, toggle, total,
                               protect);
      }
    }
  }
  for (unsigned pages : {1, 128, 1024}) {
    for (unsigned mappings : {1, 4, 32}) {
      for (bool uncommitted : {true, false}) {
        auto decommit_name =
            fbl::StringPrintf("Vmar/Decommit%s/%uMappings/%uPages",
                              uncommitted ? "Uncommitted" : "", mappings, pages * mappings);
        perftest::RegisterTest(decommit_name.c_str(), VmarDecommit, uncommitted, mappings, pages);
      }
    }
  }
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
