// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/dynamic.h>
#include <lib/elfldltl/fuzzer.h>
#include <lib/elfldltl/memory.h>
#include <lib/elfldltl/relocation.h>
#include <lib/elfldltl/symbol.h>
#include <zircon/assert.h>

#include <string>
#include <vector>

namespace {

constexpr elfldltl::DiagnosticsFlags kDiagFlags = {.multiple_errors = true};

template <class Elf>
struct DynamicFuzzer {
  using FuzzerInputs = elfldltl::FuzzerInput<
      // The fuzzer has uses two input blobs, aligned to address size.
      sizeof(typename Elf::Addr),  // Alignment
      typename Elf::Dyn,           // PT_DYNAMIC
      std::byte>;                  // Memory image

  int operator()(FuzzedDataProvider& provider) const {
    auto image_address = provider.ConsumeIntegral<typename Elf::size_type>();
    FuzzerInputs inputs(provider);
    auto [dyn, image] = inputs.inputs();
    cpp20::span image_bytes{const_cast<std::byte*>(image.data()), image.size()};
    elfldltl::DirectMemory memory(image_bytes, image_address);

    std::vector<std::string> errors;
    auto diag = elfldltl::CollectStringsDiagnostics(errors, kDiagFlags);

    elfldltl::RelocationInfo<Elf> ri;
    elfldltl::SymbolInfo<Elf> si;
    elfldltl::InitFiniInfo<Elf> init, fini;
    bool ok = elfldltl::DecodeDynamic(
        // All the observers should be used here.
        diag, memory, dyn,
        elfldltl::DynamicTextrelRejectObserver(),     //
        elfldltl::DynamicRelocationInfoObserver(ri),  //
        elfldltl::DynamicSymbolInfoObserver(si),      //
        elfldltl::DynamicInitObserver(init),          //
        elfldltl::DynamicFiniObserver(fini));

    ZX_ASSERT(diag.errors() + diag.warnings() == errors.size());

    return (ok || diag.errors() + diag.warnings() > 0) ? 0 : 1;
  }
};

using Fuzzer = elfldltl::ElfFuzzer<DynamicFuzzer>;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  return Fuzzer{}(provider);
}
