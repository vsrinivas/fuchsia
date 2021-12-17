// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "handoff-prep.h"

#include <lib/boot-options/boot-options.h>
#include <lib/llvm-profdata/llvm-profdata.h>
#include <lib/memalloc/pool-mem-config.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/trivial-allocator/new.h>
#include <stdio.h>
#include <string-file.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <phys/allocation.h>
#include <phys/handoff.h>
#include <phys/symbolize.h>

namespace {

// Carve out some physical pages requested for testing before handing off.
void FindTestRamReservation(RamReservation& ram) {
  ZX_ASSERT_MSG(!ram.paddr, "Must use kernel.test.ram.reserve=SIZE without ,ADDRESS!");

  memalloc::Pool& pool = Allocation::GetPool();

  // Don't just use Pool::Allocate because that will use the first (lowest)
  // address with space.  The kernel's PMM initialization doesn't like the
  // earliest memory being split up too small, and anyway that's not very
  // representative of just a normal machine with some device memory elsewhere,
  // which is what the test RAM reservation is really meant to simulate.
  // Instead, find the highest-addressed, most likely large chunk that is big
  // enough and just make it a little smaller, which is probably more like what
  // an actual machine with a little less RAM would look like.

  auto it = pool.end();
  while (true) {
    if (it == pool.begin()) {
      break;
    }
    --it;
    if (it->type == memalloc::Type::kFreeRam && it->size >= ram.size) {
      uint64_t aligned_start = (it->addr + it->size - ram.size) & -uint64_t{ZX_PAGE_SIZE};
      uint64_t aligned_end = aligned_start + ram.size;
      if (aligned_start >= it->addr && aligned_end <= aligned_start + ram.size) {
        if (pool.UpdateFreeRamSubranges(memalloc::Type::kTestRamReserve, aligned_start, ram.size)
                .is_ok()) {
          ram.paddr = aligned_start;
          if (gBootOptions->phys_verbose) {
            // Dump out the memory usage again to show the reservation.
            printf("%s: Physical memory after kernel.test.ram.reserve carve-out:\n",
                   Symbolize::kProgramName_);
            pool.PrintMemoryRanges(Symbolize::kProgramName_);
          }
          return;
        }
        // Don't try another spot if something went wrong.
        break;
      }
    }
  }

  printf("%s: ERROR: Cannot reserve %#" PRIx64
         " bytes of RAM for kernel.test.ram.reserve request!\n",
         Symbolize::kProgramName_, ram.size);
}

}  // namespace

void HandoffPrep::Init(ktl::span<ktl::byte> buffer) {
  // TODO(fxbug.dev/84107): Use the buffer inside the data ZBI via a
  // SingleHeapAllocator.  Later allocator() will return a real(ish) allocator.
  allocator_.allocate_function() = AllocateFunction(buffer);

  fbl::AllocChecker ac;
  handoff_ = new (allocator(), ac) PhysHandoff;
  ZX_ASSERT_MSG(ac.check(), "handoff buffer too small for PhysHandoff!");
}

void HandoffPrep::SetInstrumentation() {
  // Publish llvm-profdata if present.
  LlvmProfdata profdata;
  profdata.Init(Symbolize::GetInstance()->BuildId());
  if (profdata.size_bytes() != 0) {
    fbl::AllocChecker ac;
    ktl::span buffer = New(handoff()->instrumentation.llvm_profdata, ac, profdata.size_bytes());
    ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for llvm-profdata", profdata.size_bytes());
    ZX_DEBUG_ASSERT(buffer.size() == profdata.size_bytes());

    // Copy the fixed data and initial counter values and then start updating
    // the handoff data in place.
    ktl::span counters = profdata.WriteFixedData(buffer);
    profdata.CopyCounters(counters);
    LlvmProfdata::UseCounters(counters);
  }

  // Collect the symbolizer logging, including logs for each nonempty dump.
  SetSymbolizerLog({
      {
          .announce = LlvmProfdata::kAnnounce,
          .sink_name = LlvmProfdata::kDataSinkName,
          .vmo_name = "physboot.profraw",
          .size_bytes = profdata.size_bytes(),
      },
  });
}

void HandoffPrep::SetSymbolizerLog(ktl::initializer_list<Debugdata> dumps) {
  auto log_to = [dumps](FILE& file) {
    Symbolize symbolize(&file);
    symbolize.Context();
    for (const Debugdata& dump : dumps) {
      if (dump.size_bytes != 0) {
        symbolize.DumpFile(dump.sink_name, dump.vmo_name, dump.announce, dump.size_bytes);
      }
    }
  };

  // First generate the symbolzer log text just to count its size.
  struct CountFile {
   public:
    size_t size() const { return size_; }

    int Write(ktl::string_view str) {
      size_ += str.size();
      return static_cast<int>(str.size());
    }

   private:
    size_t size_ = 0;
  };
  CountFile counter;
  FILE count_file(&counter);
  log_to(count_file);

  // Now we can allocate the handoff buffer for that data.
  fbl::AllocChecker ac;
  ktl::span buffer = New(handoff()->instrumentation.symbolizer_log, ac, counter.size() + 1);
  ZX_ASSERT_MSG(ac.check(), "cannot allocate %zu bytes for symbolizer log", counter.size());

  // Finally, generate the same text again to fill the buffer.
  StringFile buffer_file(buffer);
  log_to(buffer_file);

  // We had to add an extra char to the buffer since StringFile wants to
  // NUL-terminate it.  But we don't want the NUL, so make it whitespace.
  ktl::move(buffer_file).take().back() = '\n';
}

BootOptions& HandoffPrep::SetBootOptions(const BootOptions& boot_options) {
  fbl::AllocChecker ac;
  BootOptions* handoff_options = New(handoff()->boot_options, ac, *gBootOptions);
  ZX_ASSERT_MSG(ac.check(), "cannot allocate handoff BootOptions!");

  if (handoff_options->test_ram_reserve) {
    FindTestRamReservation(*handoff_options->test_ram_reserve);
  }

  return *handoff_options;
}
