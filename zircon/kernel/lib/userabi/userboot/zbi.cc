// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "zbi.h"

#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include "util.h"

namespace {

constexpr const char kBootfsVmoName[] = "uncompressed-bootfs";
constexpr const char kScratchVmoName[] = "bootfs-decompression-scratch";

// This is used as the zbitl::View::CopyStorageItem callback to allocate
// scratch memory used by decompression.
class ScratchAllocator {
 public:
  class Holder {
   public:
    Holder() = delete;
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;

    // Unlike the default move constructor and move assignment operators, these
    // ensure that exactly one destructor cleans up the mapping.

    Holder(Holder&& other) {
      *this = std::move(other);
      ZX_ASSERT(*vmar_);
      ZX_ASSERT(*log_);
    }

    Holder& operator=(Holder&& other) {
      std::swap(vmar_, other.vmar_);
      std::swap(log_, other.log_);
      std::swap(mapping_, other.mapping_);
      std::swap(size_, other.size_);
      ZX_ASSERT(*vmar_);
      ZX_ASSERT(*log_);
      return *this;
    }

    Holder(const zx::vmar& vmar, const zx::debuglog& log, size_t size)
        : vmar_(vmar), log_(log), size_(size) {
      ZX_ASSERT(*vmar_);
      ZX_ASSERT(*log_);
      zx::vmo vmo;
      Do(zx::vmo::create(size, 0, &vmo), "allocate");
      Do(vmar_->map(0, vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &mapping_), "map");
      Do(vmo.set_property(ZX_PROP_NAME, kScratchVmoName, sizeof(kScratchVmoName) - 1), "name");
    }

    // zbitl::View::CopyStorageItem calls this get the scratch memory.
    void* get() const { return reinterpret_cast<void*>(mapping_); }

    ~Holder() {
      if (mapping_ != 0) {
        Do(vmar_->unmap(mapping_, size_), "unmap");
      }
    }

   private:
    void Do(zx_status_t status, const char* what) {
      check(*log_, status, "cannot %s %zu-byte VMO for %s", what, size_, kScratchVmoName);
      printl(*log_, "OK %s %zu-byte VMO for %s", what, size_, kScratchVmoName);
    }

    zx::unowned_vmar vmar_;
    zx::unowned_debuglog log_;
    uintptr_t mapping_ = 0;
    size_t size_ = 0;
  };

  ScratchAllocator() = delete;

  ScratchAllocator(const zx::vmar& vmar_self, const zx::debuglog& log)
      : vmar_(zx::unowned_vmar{vmar_self}), log_(zx::unowned_debuglog{log}) {
    ZX_ASSERT(*vmar_);
    ZX_ASSERT(*log_);
  }

  // zbitl::View::CopyStorageItem calls this to allocate scratch space.
  fitx::result<std::string_view, Holder> operator()(size_t size) const {
    return fitx::ok(Holder{*vmar_, *log_, size});
  }

 private:
  zx::unowned_vmar vmar_;
  zx::unowned_debuglog log_;
};

}  // namespace

zx::vmo GetBootfsFromZbi(const zx::debuglog& log, const zx::vmar& vmar_self,
                         const zx::vmo& zbi_vmo) {
  zbitl::PermissiveView<zbitl::MapUnownedVmo> zbi(
      zbitl::MapUnownedVmo{zx::unowned_vmo{zbi_vmo}, zx::unowned_vmar{vmar_self}});

  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    if ((*it).header->type == ZBI_TYPE_STORAGE_BOOTFS) {
      auto result = zbi.CopyStorageItem(it, ScratchAllocator{vmar_self, log});
      if (result.is_error()) {
        if (result.error_value().read_error) {
          fail(log, "cannot extract BOOTFS from ZBI: %.*s: read error: %s\n",
               static_cast<int>(result.error_value().zbi_error.size()),
               result.error_value().zbi_error.data(),
               zx_status_get_string(*result.error_value().read_error));
        } else if (result.error_value().write_error) {
          fail(log, "cannot extract BOOTFS from ZBI: %.*s: write error: %s\n",
               static_cast<int>(result.error_value().zbi_error.size()),
               result.error_value().zbi_error.data(),
               zx_status_get_string(*result.error_value().write_error));
        } else {
          fail(log, "cannot extract BOOTFS from ZBI: %.*s\n",
               static_cast<int>(result.error_value().zbi_error.size()),
               result.error_value().zbi_error.data());
        }
      }

      zx::vmo bootfs_vmo = std::move(result).value().release();
      check(log, bootfs_vmo.set_property(ZX_PROP_NAME, kBootfsVmoName, sizeof(kBootfsVmoName) - 1),
            "cannot set name of uncompressed BOOTFS VMO");

      // Signal that we've already processed this one.
      // GCC's -Wmissing-field-initializers is buggy: it should allow
      // designated initializers without all fields, but doesn't (in C++?).
      zbi_header_t discard{};
      discard.type = ZBI_TYPE_DISCARD;
      if (auto ok = zbi.EditHeader(it, discard); ok.is_error()) {
        check(log, ok.error_value(), "zx_vmo_write failed on ZBI VMO\n");
      }

      // Cancel error-checking since we're ending the iteration on purpose.
      zbi.ignore_error();
      return bootfs_vmo;
    }
  }

  if (auto check = zbi.take_error(); check.is_error()) {
    auto error = check.error_value();
    if (error.storage_error) {
      fail(log, "invalid ZBI: %.*s at offset %#x\n", static_cast<int>(error.zbi_error.size()),
           error.zbi_error.data(), error.item_offset);
    } else {
      fail(log, "invalid ZBI: %.*s at offset %#x: %s\n", static_cast<int>(error.zbi_error.size()),
           error.zbi_error.data(), error.item_offset, zx_status_get_string(*error.storage_error));
    }
  } else {
    fail(log, "no '/boot' bootfs in bootstrap message\n");
  }
}
