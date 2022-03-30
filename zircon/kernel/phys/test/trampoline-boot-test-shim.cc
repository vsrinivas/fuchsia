// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/item.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <cstdio>
#include <cstdlib>

#include <fbl/alloc_checker.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>
#include <phys/boot-zbi.h>
#include <phys/new.h>
#include <phys/symbolize.h>
#include <phys/trampoline-boot.h>
#include <pretty/cpp/sizes.h>

#include "turducken.h"

#include <ktl/enforce.h>

#define span_arg(s) static_cast<int>(s.size()), reinterpret_cast<const char*>(s.data())

namespace {

// When set, dictates random decisions done by the trampoline boot test.
constexpr ktl::string_view kSeedOpt = "trampoline.seed=";

// Used to communicate to the next kernel item what the expected load address is.
// If provided, will fix the value to a specific load address.
constexpr ktl::string_view kLoadAddressOpt = "trampoline.load_address=";

// Pick an allocation range from available ranges in the |memalloc::Pool|.
// Coalesce all allocatable ranges, that is, any non null region, reserved or peripheral range.
uint64_t FindAllocationRange(BootZbi::Size size, uint64_t& seed) {
  // TODO(fxb/88583): Remove stub when actually implementing random address selection.
  return 1 << 20;
}

}  // namespace

const char Symbolize::kProgramName_[] = "trampoline-boot-shim-test";

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  auto seed_opt = OptionWithPrefix(kSeedOpt);
  uint64_t load_address = 0;
  uint64_t seed = 0;

  auto load_addr_opt = OptionWithPrefix(kLoadAddressOpt);
  if (load_addr_opt) {
    auto maybe_load_addr = pretty::ParseSizeBytes(load_addr_opt.value());
    ZX_ASSERT_MSG(maybe_load_addr, "%.*s contains invalid value %.*s", span_arg(kLoadAddressOpt),
                  span_arg(load_addr_opt.value()));
    load_address = *maybe_load_addr;
  }

  if (seed_opt) {
    if (auto seed_val = ParseUint(seed_opt.value())) {
      seed = *seed_val;
      rand_r(&seed);
      uint32_t kernel_size = zbitl::UncompressedLength(*kernel_item->header);
      auto alloc = BootZbi::SuggestedAllocation(kernel_size);

      if (!load_addr_opt) {
        load_address = FindAllocationRange(alloc, seed);
      }
    }
  }

  // Temporary until trampoline boot is parametrized.
  load_address = 2 * TrampolineBoot::kLegacyLoadAddress;
  // Accommodate for snprintf writing a null terminator, instead of truncating the string.
  // Hex string: 16
  // 0x prefix: 2
  // NUL termination: 1
  uint32_t load_address_str_length = kLoadAddressOpt.length() + 16 + 2 + 1;
  uint32_t cmdline_item_length =
      ZBI_ALIGN(static_cast<uint32_t>(sizeof(zbi_header_t)) + load_address_str_length);

  set_kernel_load_address(load_address);
  Load(kernel_item, kernel_item, boot_zbi().end(), cmdline_item_length);

  // Append the new option.

  auto it_or = loaded_zbi().Append(
      {.type = ZBI_TYPE_CMDLINE, .length = static_cast<uint32_t>(load_address_str_length)});
  ZX_ASSERT(it_or.is_ok());
  auto buffer = it_or->payload;
  uint32_t written_bytes = snprintf(reinterpret_cast<char*>(buffer.data()), buffer.size(),
                                    "%.*s0x%016" PRIx64, span_arg(kLoadAddressOpt), load_address);

  // Remove the extra char to accommodate the added null terminator.
  ZX_ASSERT_MSG(written_bytes == load_address_str_length - 1,
                "written_bytes %" PRIu32 " load_address_str_length %" PRIu32, written_bytes,
                load_address_str_length);
  Boot();
  /*NOTREACHED*/
}
