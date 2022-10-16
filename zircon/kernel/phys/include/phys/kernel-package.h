// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_KERNEL_PACKAGE_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_KERNEL_PACKAGE_H_

#include <lib/arch/ticks.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>

#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/handoff.h>

// The default kernel package (i.e., STORAGE_KERNEL BOOTFS namespace) in which
// we will pick a kernel ZBI to boot.
//
// TODO(fxbug.dev/68585): Support kernel page selection via a boot option.
constexpr ktl::string_view kDefaultKernelPackage = "zircon";

// The name of the kernel ZBI within a kernel package.
constexpr ktl::string_view kKernelZbiName = "kernel.zbi";

class KernelStorage {
 public:
  using Zbi = zbitl::View<ktl::span<ktl::byte>>;
  using Bootfs = zbitl::BootfsView<ktl::span<const ktl::byte>>;

  KernelStorage() noexcept = default;

  KernelStorage(KernelStorage&&) noexcept = default;

  // Unpacks the ZBI_TYPE_KERNEL_STORAGE item from the ZBI.
  void Init(Zbi zbi);

  Zbi& zbi() { return zbi_; }
  const Zbi& zbi() const { return zbi_; }

  // Return the position in the input ZBI where KERNEL_STORAGE was found.
  Zbi::iterator item() const { return item_; }

  // Return the unpacked ZBI_BOOTFS_PAGE_SIZE-aligned buffer owned by this
  // object.
  auto data() const { return storage_.data(); }

  // Helper to decode data() as a BOOTFS image.
  fit::result<Bootfs::Error, Bootfs> GetBootfs(ktl::string_view directory) const {
    return bootfs_reader_.root().subdir(directory);
  }

  void GetTimes(PhysBootTimes& times) {
    times.Set(PhysBootTimes::kDecompressStart, decompress_start_ts_);
    times.Set(PhysBootTimes::kDecompressEnd, decompress_end_ts_);
  }

 private:
  using BootfsReader = zbitl::Bootfs<ktl::span<const ktl::byte>>;

  Allocation storage_;
  Zbi zbi_;
  Zbi::iterator item_;
  BootfsReader bootfs_reader_;
  arch::EarlyTicks decompress_start_ts_, decompress_end_ts_;
};

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_KERNEL_PACKAGE_H_
