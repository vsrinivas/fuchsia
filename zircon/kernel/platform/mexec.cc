// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crashlog.h>
#include <lib/zbitl/error-stdio.h>
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
#include <ktl/variant.h>
#include <lk/init.h>
#include <phys/handoff.h>
#include <vm/vm_object.h>

namespace {

// Mexec data as gleaned from the physboot hand-off.
zbitl::Image<fbl::Array<ktl::byte>> gImageAtHandoff;

void ConstructMexecDataZbi(uint level) {
  constexpr size_t kInitialBuffSize = ZX_PAGE_SIZE;

  ZX_ASSERT(!gImageAtHandoff.storage());
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

  // Forward relevant items from the physboot hand-off.
  ZX_ASSERT(gPhysHandoff != nullptr);

  if (auto result = ArchAppendMexecDataFromHandoff(gImageAtHandoff, *gPhysHandoff);
      result.is_error()) {
    abort();
  }

  // Appends the appropriate UART config, as encoded in the hand-off, which is
  // given as variant of libuart driver types, each with methods to indicate
  // the ZBI item type and payload.
  constexpr auto append_uart_item = [](const auto& uart) {
    if (uart.extra() == 0) {  // The null driver.
      return;
    }
    const zbi_header_t header = {
        .type = ZBI_TYPE_KERNEL_DRIVER,
        .extra = uart.extra(),
    };
    auto result = gImageAtHandoff.Append(header, zbitl::AsBytes(uart.config()));
    if (result.is_error()) {
      printf("mexec: could not append UART driver config: ");
      zbitl::PrintViewError(result.error_value());
      abort();
    }
  };

  ktl::visit(append_uart_item, gPhysHandoff->serial);

  if (gPhysHandoff->nvram) {
    auto result = gImageAtHandoff.Append(zbi_header_t{.type = ZBI_TYPE_NVRAM},
                                         zbitl::AsBytes(gPhysHandoff->nvram.value()));
    if (result.is_error()) {
      printf("mexec: could not append NVRAM region: ");
      zbitl::PrintViewError(result.error_value());
      abort();
    }
  }

  if (gPhysHandoff->platform_id) {
    auto result = gImageAtHandoff.Append(zbi_header_t{.type = ZBI_TYPE_PLATFORM_ID},
                                         zbitl::AsBytes(gPhysHandoff->platform_id.value()));
    if (result.is_error()) {
      printf("mexec: could not append platform ID: ");
      zbitl::PrintViewError(result.error_value());
      abort();
    }
  }
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
      printf("mexec: could not append crashlog: ");
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
