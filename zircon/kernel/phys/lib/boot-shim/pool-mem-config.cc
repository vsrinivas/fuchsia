// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/boot-shim/pool-mem-config.h>
#include <lib/memalloc/pool.h>
#include <lib/stdcompat/span.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <optional>

namespace {

using ErrorType = boot_shim::ItemBase::DataZbi::Error;

// Iterate over the (discontiguous) pool ranges in address order.  Reduce the
// pool range types to the basic ZBI types and coalesce adjacent entries that
// have the same type after reduction.  Each resulting entry is passed to the
// callback in `sum = callback(entry, sum);` and the final sum returned.
constexpr auto Accumulate = [](const memalloc::Pool& pool, auto&& callback,
                               auto sum) -> decltype(sum) {
  std::optional<zbi_mem_range_t> pending;
  for (const memalloc::MemRange& range : pool) {
    const zbi_mem_range_t new_range = {
        .paddr = range.addr,
        .length = range.size,
        .type = memalloc::IsExtendedType(range.type)  // Reduce to basic types.
                    ? ZBI_MEM_RANGE_RAM
                    : static_cast<uint32_t>(range.type),
    };
    if (!pending) {
      // First range.
      pending = new_range;
    } else if (pending->type == new_range.type &&
               pending->paddr + pending->length == new_range.paddr) {
      // Coalesce with the pending contiguous range.
      pending->length += new_range.length;
    } else {
      // Discontiguous with the pending range or a different type.
      // Complete the pending entry and make the new one pending.
      sum = callback(*pending, sum);
      pending = new_range;
    }
  }
  // Complete the final entry.
  if (pending) {
    sum = callback(*pending, sum);
  }
  return sum;
};

size_t Count(const zbi_mem_range_t& entry, size_t sum) { return sum + sizeof(entry); }

cpp20::span<zbi_mem_range_t> Write(const zbi_mem_range_t& entry,
                                   cpp20::span<zbi_mem_range_t> payload) {
  payload.front() = entry;
  return payload.subspan(1);
}

size_t PayloadSize(const memalloc::Pool& pool) { return Accumulate(pool, Count, 0); }

void WritePayload(const memalloc::Pool& pool, cpp20::span<std::byte> buffer) {
  cpp20::span<zbi_mem_range_t> payload{
      reinterpret_cast<zbi_mem_range_t*>(buffer.data()),
      buffer.size_bytes() / sizeof(zbi_mem_range_t),
  };
  payload = Accumulate(pool, Write, payload);
  ZX_DEBUG_ASSERT(payload.empty());
}

}  // namespace

namespace boot_shim {

size_t PoolMemConfigItem::size_bytes() const { return pool_ ? ItemSize(PayloadSize(*pool_)) : 0; }

fitx::result<ErrorType> PoolMemConfigItem::AppendItems(DataZbi& zbi) const {
  const size_t payload_size = pool_ ? PayloadSize(*pool_) : 0;
  if (payload_size > 0) {
    auto result = zbi.Append({
        .type = ZBI_TYPE_MEM_CONFIG,
        .length = static_cast<uint32_t>(payload_size),
    });
    if (result.is_error()) {
      return result.take_error();
    }
    WritePayload(*pool_, result->payload);
  }
  return fitx::ok();
}

}  // namespace boot_shim
