// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "handoff-prep.h"

#include <lib/llvm-profdata/llvm-profdata.h>
#include <string-file.h>
#include <zircon/assert.h>

#include <ktl/algorithm.h>
#include <phys/handoff.h>
#include <phys/symbolize.h>

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
