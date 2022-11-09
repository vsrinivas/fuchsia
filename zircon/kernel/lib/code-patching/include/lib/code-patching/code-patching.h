// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_
#define ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_

#include <lib/arch/cache.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/zbitl/items/bootfs.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/optional.h>
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
// directory with the following expected contents:
//
// * code-patches.bin
//   This is raw binary comprised of an array of patch directives (in practice,
//   removed as a section from the executable to patch).
//
// * code-patches/
//   A subdirectory under which patch alternatives are found.
//
// Patcher provides methods for patching provided instruction ranges in the
// supported ways (e.g., nop-fill or wholesale replacement by an alternative).
//
// This just modifies code in memory and does no synchronization.
// No synchronization is usually required when modifying code just
// loaded into memory pages that have never been executed yet.
class Patcher {
 public:
  using Bytes = ktl::span<ktl::byte>;
  using BootfsDir = zbitl::BootfsView<Bytes>;
  using Error = BootfsDir::Error;

  using iterator = ktl::span<const Directive>::iterator;

  // The file containing the Directives.
  static constexpr ktl::string_view kPatchesBin = "code-patches.bin";

  // A directory under which patch alternatives are found.
  static constexpr ktl::string_view kPatchAlternativeDir = "code-patches";

  Patcher() = default;

  // Initializes the Patcher. The associated BOOTFS directory namespace must be
  // nonempty. Must be called before any other method. On initialization, the
  // lifetime of the Patcher is bound to that of the original BootfsView input.
  fit::result<Error> Init(BootfsDir bootfs);

  // The associated patch directives.
  ktl::span<const Directive> patches() const { return patches_; }

  // Replaces a range of instructions with the given patch alternative.
  fit::result<Error> PatchWithAlternative(ktl::span<ktl::byte> instructions,
                                          ktl::string_view alternative);

  // Overwrites a range of instuctions with the minimal number of `nop`
  // instructions.
  void NopFill(ktl::span<ktl::byte> instructions);

 protected:
  using SyncFunction = fit::inline_function<void(ktl::span<ktl::byte>)>;

  explicit Patcher(SyncFunction sync) : sync_(ktl::move(sync)) {}

 private:
  fit::result<Error, Bytes> GetPatchAlternative(ktl::string_view name);

  BootfsDir bootfs_;
  ktl::span<const Directive> patches_;
  SyncFunction sync_{[](ktl::span<ktl::byte> instructions) {}};
};

// This is the same as code_patching::Patcher, but Instruction-cache coherence
// among the modified ranges is also managed by the class: it will be effected
// on destruction or each time Commit() is called.
//
// This should be used when the patches are being applied to code that has
// already been loaded into pages that might have been executed.
class PatcherWithGlobalCacheConsistency : public Patcher {
 public:
  PatcherWithGlobalCacheConsistency()
      : Patcher([this](ktl::span<ktl::byte> instructions) { Sync(instructions); }) {}

  // Forces instruction-data cache consistency among the modified ranges since
  // construction or when this method was last called.  In general, it is not
  // required that this method be called; consistency will also be reached upon
  // destruction of the PatcherWithGlobalCacheConsistency object.
  void Commit() {
    // Destroying the old *sync_ctx_ object makes it synchronize the caches.
    // The new identical object will synchronize again when *this is destroyed.
    sync_ctx_.~SyncCtx();
    new (&sync_ctx_) SyncCtx();
  }

 private:
  using SyncCtx = arch::GlobalCacheConsistencyContext;

  void Sync(ktl::span<ktl::byte> instructions) {
    sync_ctx_.SyncRange(reinterpret_cast<uintptr_t>(instructions.data()), instructions.size());
  }

  SyncCtx sync_ctx_;
};

void PrintPatcherError(const Patcher::Error& error, FILE* f = stdout);

}  // namespace code_patching

#endif  // ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_
