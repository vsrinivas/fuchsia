// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/fuzzer.h>
#include <lib/elfldltl/layout.h>
#include <lib/elfldltl/relocation.h>

namespace {

template <class Elf>
struct RelocationFuzzer {
  using RelocInfo = elfldltl::RelocationInfo<Elf>;
  using size_type = typename RelocInfo::size_type;

  template <typename Jmprel>
  using FuzzerInputs = elfldltl::FuzzerInput<  // Four separate inputs,
      sizeof(typename RelocInfo::Addr),        // each aligned to address size:
      typename RelocInfo::Rel,                 // 1. DT_REL
      typename RelocInfo::Rela,                // 2. DT_RELA
      typename RelocInfo::Addr,                // 3. DT_RELR
      Jmprel>;                                 // 4. DT_JMPREL

  using InputsRela = FuzzerInputs<typename RelocInfo::Rela>;
  using InputsRel = FuzzerInputs<typename RelocInfo::Rel>;

  int operator()(FuzzedDataProvider& provider) const {
    // Collect a few bits from the provider first.
    bool jmprel_is_rela = provider.ConsumeBool();
    size_type relcount = provider.ConsumeIntegral<size_type>();
    size_type relacount = provider.ConsumeIntegral<size_type>();

    // This will get four data blobs that exhaust the provider.
    auto fuzz = [&](auto rel, auto rela, auto relr, auto jmprel) -> bool {
      RelocInfo info;
      info.set_rel(rel, relcount).set_rela(rela, relacount).set_jmprel(jmprel);
      // These walks never fail for any "bad data" reasons, they should just
      // call the visitor.  It never fails, so the walks should never fail.
      constexpr auto visitor = [](auto&& reloc) -> bool { return true; };
      return info.VisitRelative(visitor) && info.VisitSymbolic(visitor);
    };

    // Consume all remaining data with input blobs, the last of whichever type.
    bool ok = jmprel_is_rela ? std::apply(fuzz, InputsRela(provider).inputs())
                             : std::apply(fuzz, InputsRel(provider).inputs());
    return ok ? 0 : 1;
  }
};

using Fuzzer = elfldltl::ElfFuzzer<RelocationFuzzer>;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  return Fuzzer{}(provider);
}
