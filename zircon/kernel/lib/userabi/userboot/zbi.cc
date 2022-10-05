// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "zbi.h"

#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/view.h>
#include <lib/zbitl/vmo.h>
#include <lib/zircon-internal/align.h>
#include <stdarg.h>
#include <zircon/boot/image.h>
#include <zircon/status.h>

#include "option.h"
#include "util.h"

namespace {

using namespace std::string_view_literals;

using ZbiView = zbitl::View<zbitl::MapUnownedVmo>;
using ZbiError = ZbiView::Error;
using ZbiCopyError = ZbiView::CopyError<zbitl::MapOwnedVmo>;

constexpr const char kBootfsVmoName[] = "uncompressed-bootfs";
constexpr const char kScratchVmoName[] = "bootfs-decompression-scratch";

[[noreturn]] void FailFromZbiError(const ZbiError& error, const zx::debuglog& log) {
  zbitl::PrintViewError(error, [&](const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printl(log, fmt, args);
    va_end(args);
  });
  zx_process_exit(-1);
}

[[noreturn]] void FailFromZbiCopyError(const ZbiCopyError& error, const zx::debuglog& log) {
  zbitl::PrintViewCopyError(error, [&](const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printl(log, fmt, args);
    va_end(args);
  });
  zx_process_exit(-1);
}

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
      Do(vmar_->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &mapping_), "map");
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
  fit::result<std::string_view, Holder> operator()(size_t size) const {
    return fit::ok(Holder{*vmar_, *log_, size});
  }

 private:
  zx::unowned_vmar vmar_;
  zx::unowned_debuglog log_;
};

}  // namespace

zx::vmo GetBootfsFromZbi(const zx::debuglog& log, const zx::vmar& vmar_self,
                         const zx::vmo& zbi_vmo) {
  ZbiView zbi(zbitl::MapUnownedVmo{zx::unowned_vmo{zbi_vmo}, /*writable=*/true,
                                   zx::unowned_vmar{vmar_self}});

  for (auto it = zbi.begin(); it != zbi.end(); ++it) {
    if (it->header->type == ZBI_TYPE_STORAGE_BOOTFS) {
      auto result = zbi.CopyStorageItem(it, ScratchAllocator{vmar_self, log});
      if (result.is_error()) {
        printl(log, "cannot extract BOOTFS from ZBI: ");
        FailFromZbiCopyError(result.error_value(), log);
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
    printl(log, "invalid ZBI: ");
    FailFromZbiError(check.error_value(), log);
  }

  fail(log, "no '/boot' bootfs in bootstrap message\n");
  __UNREACHABLE;
}

Options GetOptionsFromZbi(const zx::debuglog& log, const zx::vmar& vmar_self, const zx::vmo& zbi) {
  ZbiView view(zbitl::MapUnownedVmo{
      zx::unowned_vmo{zbi},
      /*writable=*/false,
      zx::unowned_vmar{vmar_self},
  });
  Options opts;
  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    if (header->type != ZBI_TYPE_CMDLINE) {
      continue;
    }

    // Map in and parse the CMDLINE payload. The strings referenced by `opts`
    // will be owned by the mapped pages and will be valid within `vmar_self`'s
    // lifetime (i.e., for the entirety of userboot's runtime).
    const uint64_t previous_page_boundary = payload & -ZX_PAGE_SIZE;
    const uint64_t next_page_boundary = ZX_PAGE_ALIGN(payload + header->length);
    const size_t size = next_page_boundary - previous_page_boundary;
    uintptr_t mapping;
    if (zx_status_t status =
            vmar_self.map(ZX_VM_PERM_READ, 0, zbi, previous_page_boundary, size, &mapping);
        status != ZX_OK) {
      fail(log, "failed to map CMDLINE item: %s", zx_status_get_string(status));
    }

    const uintptr_t mapped_payload = mapping + (payload % ZX_PAGE_SIZE);
    std::string_view cmdline{reinterpret_cast<const char*>(mapped_payload), header->length};
    printl(log, "CMDLINE %.*s\n", static_cast<int>(cmdline.size()), cmdline.data());
    ParseCmdline(log, cmdline, opts);
  }

  if (auto result = view.take_error(); result.is_error()) {
    printl(log, "invalid ZBI: ");
    FailFromZbiError(result.error_value(), log);
  }

  return opts;
}
