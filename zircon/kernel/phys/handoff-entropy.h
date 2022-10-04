// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_HANDOFF_ENTROPY_H_
#define ZIRCON_KERNEL_PHYS_HANDOFF_ENTROPY_H_

#include <lib/boot-options/boot-options.h>
#include <lib/crypto/entropy_pool.h>
#include <lib/fit/result.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/boot/image.h>

#include <ktl/byte.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <phys/stdio.h>

// Thin wrapper for the handoff process of entropy related items and options.
// This involves policies regarding entropy consumption and boot requirements.
class EntropyHandoff {
 public:
  using Zbi = zbitl::View<ktl::span<ktl::byte>>;
  using ZbiIterator = Zbi::iterator;

  EntropyHandoff() = default;
  EntropyHandoff(FILE* log) : log_(log) {}

  // Adds |payload| to the underlying entropy pool.
  // Entropy is redacted with an arbitrary value.
  void AddEntropy(ktl::span<ktl::byte> payload);

  // Adds entropy provided through |options| to the underlying entropy pool.
  // Entropy is redacted with an arbitrary value.
  void AddEntropy(BootOptions& options);

  // Return true, if the entropy handoff collected enough entropy to successfully produce
  // an |EntropyPool|.
  bool HasEnoughEntropy() const { return has_valid_item_; }

  // If enough entropy was collected and all boot options requirements are met, an entropy
  // pool with the collected entropy is returned.
  // If the conditions are not met, the program is aborted.
  ktl::optional<crypto::EntropyPool> Take(const BootOptions& options) &&;

 private:
  crypto::EntropyPool pool_;

  FILE* log_ = PhysConsole::Get().null();
  bool has_valid_item_ = false;
};

#endif  // ZIRCON_KERNEL_PHYS_HANDOFF_ENTROPY_H_
