// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crashlog.h>
#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <lib/zx/status.h>
#include <mexec.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/ref_ptr.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/type_traits.h>
#include <lk/init.h>
#include <vm/vm_object.h>

namespace {

// Mexec data as gleaned from the physboot hand-off.
zbitl::Image<fbl::Array<ktl::byte>> gImageAtHandoff;

void ConstructMexecDataZbi(uint level) {
  constexpr size_t kInitialBuffSize = ZX_PAGE_SIZE;

  ZX_DEBUG_ASSERT(!gImageAtHandoff.storage());
  {
    fbl::AllocChecker ac;
    auto* buff = new (&ac) ktl::byte[kInitialBuffSize];
    ZX_ASSERT(ac.check());
    ZX_ASSERT(buff != nullptr);
    gImageAtHandoff.storage() = fbl::Array<ktl::byte>(buff, kInitialBuffSize);
  }

  if (auto result = gImageAtHandoff.clear(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    abort();
  }

  // TODO(fxbug.dev/88059): Copy data over from gPhysHandoff.
}

}  // namespace

// After the VM is initialized so that we can allocate.
LK_INIT_HOOK(construct_mexec_data_zbi, ConstructMexecDataZbi, LK_INIT_LEVEL_VM)

zx::status<size_t> WriteMexecData(ktl::span<ktl::byte> buffer) {
  // Storage or write errors resulting from a span-backed Image imply buffer
  // overflow.
  constexpr auto error = [](const auto& err) -> zx::status<size_t> {
    return zx::error{err.storage_error ? ZX_ERR_BUFFER_TOO_SMALL : ZX_ERR_INTERNAL};
  };
  constexpr auto extend_error = [](const auto& err) -> zx::status<size_t> {
    return zx::error{err.write_error ? ZX_ERR_BUFFER_TOO_SMALL : ZX_ERR_INTERNAL};
  };

  zbitl::Image image(buffer);
  if (auto result = image.clear(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    return error(result.error_value());
  }

  if (auto result = image.Extend(gImageAtHandoff.begin(), gImageAtHandoff.end());
      result.is_error()) {
    zbitl::PrintViewCopyError(result.error_value());
    return extend_error(result.error_value());
  }

  if (auto result = gImageAtHandoff.take_error(); result.is_error()) {
    zbitl::PrintViewError(result.error_value());
    return zx::error{ZX_ERR_INTERNAL};
  }

  // TODO(fxbug.dev/88059): The items appended here will eventually be appended
  // in ConstructMexecDataZbi() above as a function of the physboot hand-off.
  if (zx_status_t status = platform_append_mexec_data(image.storage()); status != ZX_OK) {
    return zx::error{status};
  }

  // Propagate any stashed crashlog to the next kernel.
  if (const fbl::RefPtr<VmObject> crashlog = crashlog_get_stashed()) {
    const zbi_header_t header = {
        .type = ZBI_TYPE_CRASHLOG,
        .length = static_cast<uint32_t>(crashlog->size()),
    };
    if (auto result = image.Append(header); result.is_error()) {
      printf("GetMexecDataZbi: could not append crashlog: ");
      zbitl::PrintViewError(result.error_value());
      return error(result.error_value());
    } else {
      auto it = ktl::move(result).value();
      ktl::span<ktl::byte> payload = it->payload;
      zx_status_t status = crashlog->Read(payload.data(), 0, payload.size());
      if (status != ZX_OK) {
        return zx::error{status};
      }
    }
  }

  return zx::ok(image.size_bytes());
}
