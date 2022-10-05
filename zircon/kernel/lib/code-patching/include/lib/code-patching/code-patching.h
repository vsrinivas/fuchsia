// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_
#define ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_

#include <lib/arch/cache.h>
#include <lib/fit/result.h>
#include <lib/zbitl/items/bootfs.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstddef>
#include <cstring>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

// This file is concerned with the facilities for code-patching.

namespace code_patching {

// A patch directive, giving the 'what' of an instruction range and the 'how'
// and 'when' of a patch case identifier.
struct Directive {
  uint64_t range_start;
  uint32_t range_size;

  // A patch case identifier, corresponding to particular hard-coded details on
  // how and when code should be the replaced.
  uint32_t id;
};

// Ensures against alignment padding.
static_assert(std::has_unique_object_representations_v<Directive>);

// Patcher helps to facilitate code patching. It is constructed from a BOOTFS
// that expects the following entries to be present for some directory
// namespace:
//
// * ${NAMESPACE}/code-patches.bin
//   This is raw binary comprised of an array of patch directives (in practice,
//   removed as a section from the executable to patch).
//
// * ${NAMESPACE}/code-patches/
//   A directory under which patch alternatives are found.
//
// Patcher provides methods for patching provided instruction ranges in the
// supported ways (e.g., nop-fill or wholesale replacement by an alternative).
// Instruction-cache coherence among the modified ranges is also managed by the
// class: it will be effected on destruction or once Commit() is called.
//
class Patcher {
 public:
  using Bytes = ktl::span<const ktl::byte>;
  using Bootfs = zbitl::BootfsView<Bytes>;
  using Error = Bootfs::Error;

  using iterator = ktl::span<const Directive>::iterator;

  // The file containing the Directives.
  static constexpr ktl::string_view kPatchesBin = "code-patches.bin";

  // A directory under which patch alternatives are found.
  static constexpr ktl::string_view kPatchAlternativeDir = "code-patches";

  // Initializes the Patcher. The provided directory namespace must be
  // nonempty. Must be called before any other method.
  fit::result<Error> Init(Bootfs bootfs, ktl::string_view directory);

  // The associated patch directives.
  ktl::span<const Directive> patches() const { return patches_; }

  // Replaces a range of instructions with the given patch alternative.
  fit::result<Error> PatchWithAlternative(ktl::span<ktl::byte> instructions,
                                          ktl::string_view alternative);

  // Overwrites a range of instuctions with the minimal number of `nop`
  // instructions.
  void NopFill(ktl::span<ktl::byte> instructions);

  // Forces instruction-data cache consistency among the modified ranges since
  // construction or when this method was last called. In general, it is not
  // required that this method be called; consistency will also be reached upon
  // destruction of Patcher.
  void Commit() { sync_ctx_ = {}; }

 private:
  fit::result<Error, Bytes> GetPatchAlternative(ktl::string_view name);

  void PrepareToSync(ktl::span<ktl::byte> instructions) {
    sync_ctx_.SyncRange(reinterpret_cast<uintptr_t>(instructions.data()), instructions.size());
  }

  Bootfs bootfs_;
  ktl::string_view dir_;
  ktl::span<const Directive> patches_;
  arch::GlobalCacheConsistencyContext sync_ctx_;
};

void PrintPatcherError(const Patcher::Error& error, FILE* f = stdout);

}  // namespace code_patching

#endif  // ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_
