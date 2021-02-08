// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_BOOT_ZBI_H_
#define ZIRCON_KERNEL_PHYS_BOOT_ZBI_H_

#include <lib/fitx/result.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <cstdint>

#include <ktl/span.h>

// This class manages loading a ZBI kernel and its data ZBI into memory and
// booting it.  The caller first uses BootZbi::GetSizes() to determine the
// minimum physical memory required and does its own allocations; the caller
// can increase its allocation for the data ZBI if it needs to add items.  Then
// a BootZbi object is primed with the Load() call, which fills in the memory
// just allocated with the kernel and data ZBI images from the incoming ZBI.
// The caller then has the opportunity to amend the data ZBI via the data()
// accessor, and finally it calls Boot() to transfer control to the ZBI kernel.

class BootZbi {
 public:
  using Bytes = ktl::span<std::byte>;
  using InputZbi = zbitl::PermissiveView<zbitl::ByteView>;
  using Zbi = zbitl::PermissiveImage<Bytes>;
  using Error = InputZbi::CopyError<Bytes>;

  struct Size {
    size_t size, alignment;
  };

  struct Sizes {
    Size kernel, data;
  };

  BootZbi() = default;

  BootZbi(const BootZbi&) = default;

  // Use GetSizes(), below, to compute required sizes and alignments to
  // allocate. The BootZbi object does not take ownership of the buffers.
  // Their data can be safely left uninitialized.
  BootZbi(Bytes kernel, Bytes data) : kernel_(kernel), data_(data) {}

  // Take the ZBI from the boot loader and compute the minimum sizes and
  // alignments that must be allocated to hold the kernel and the data ZBI.
  // The data.size can be increased before use if appending additional items.
  // If the kernel.size is zero, then Load() can reuse the input ZBI memory.
  static fitx::result<Error, Sizes> GetSizes(InputZbi zbi);

  // Suggest allocation parameters for a whole bootable ZBI image whose
  // incoming size is known but whose contents haven't been seen yet.  A
  // conforming allocation will be optimal for reuse by Load().
  static Size SuggestedAllocation(uint32_t zbi_size_bytes);

  // Split the ZBI from the boot loader into the kernel and data ZBIs using the
  // space allocated for each.  If data.size() > GetSizes(zbi)->data.size then
  // data().Append and .Extend can be used to add items to the data ZBI later.
  // If created with an empty kernel buffer, Boot() will reuse zbi.storage()
  // directly instead of copying the kernel.  Otherwise, zbi is no longer
  // referenced after Load().
  fitx::result<Error> Load(InputZbi zbi);

  // This can be used after successful Load() to modify the data ZBI.
  Zbi& data() { return data_; }

  // Boot into the kernel loaded by Load(), which must have been called first.
  // This cannot fail and never returns.
  [[noreturn]] void Boot();

 private:
  Zbi kernel_, data_;
};

#endif  // ZIRCON_KERNEL_PHYS_BOOT_ZBI_H_
