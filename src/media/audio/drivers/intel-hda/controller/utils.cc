// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <lib/ddk/driver.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/intel-hda.h>
#include <zircon/process.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <intel-hda/utils/intel-hda-registers.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {

fbl::RefPtr<fzl::VmarManager> CreateDriverVmars() {
  // Create a compact VMAR to map all of our registers into.
  //
  // TODO(johngro): See fxbug.dev/31691 for details.
  //
  // Sizing right now is a bit of a guessing game.  A compact VMAR is not
  // going to perfectly tightly pack everything; it will still insert random
  // gaps in an attempt to get some minimum level of ASLR.  For now, I'm using
  // hardcoded guidance from teisenbe@ about how to size for the worst case.
  // If/when there is a better way of doing this, I need to come back and
  // switch to that.
  //
  // Formula being used here should be...
  // 2 * (total_region_size + (512k * (total_allocations - 1)))
  const size_t kPageSize = zx_system_get_page_size();
  const size_t max_size_per_controller =
      sizeof(hda_all_registers_t) + ZX_ROUNDUP(MAPPED_CORB_RIRB_SIZE, kPageSize) +
      (MAX_STREAMS_PER_CONTROLLER * ZX_ROUNDUP(MAPPED_BDL_SIZE, kPageSize)) +
      sizeof(adsp_registers_t) + ZX_ROUNDUP(MAPPED_BDL_SIZE, kPageSize);
  // One alloc for the main registers, one for code loader BDL.
  constexpr size_t MAX_ALLOCS_PER_DSP = 2;
  // One alloc for the main registers, one for the CORB/RIRB, two for DSP,
  // and one for each possible stream BDL.
  const size_t max_allocs_per_controller = 2 + MAX_ALLOCS_PER_DSP + MAX_STREAMS_PER_CONTROLLER;
  const size_t VMAR_SIZE =
      2 * (max_size_per_controller + ((max_allocs_per_controller - 1) * (512u << 10)));

  GLOBAL_LOG(DEBUG, "Allocating 0x%zx byte VMAR for registers.", VMAR_SIZE);

  return fzl::VmarManager::Create(VMAR_SIZE);
}

}  // namespace intel_hda
}  // namespace audio
