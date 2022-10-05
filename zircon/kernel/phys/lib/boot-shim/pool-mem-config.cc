// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-shim/pool-mem-config.h>
#include <lib/memalloc/pool-mem-config.h>
#include <lib/stdcompat/span.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <algorithm>
#include <optional>

namespace {

using ErrorType = boot_shim::ItemBase::DataZbi::Error;

size_t PayloadSize(const memalloc::Pool& pool) {
  memalloc::PoolMemConfig mem_config(pool);
  return std::distance(mem_config.begin(), mem_config.end()) * sizeof(zbi_mem_range_t);
}

void WritePayload(const memalloc::Pool& pool, cpp20::span<std::byte> buffer) {
  memalloc::PoolMemConfig mem_config(pool);
  cpp20::span<zbi_mem_range_t> payload{
      reinterpret_cast<zbi_mem_range_t*>(buffer.data()),
      buffer.size_bytes() / sizeof(zbi_mem_range_t),
  };
  std::copy(mem_config.begin(), mem_config.end(), payload.begin());
}

}  // namespace

namespace boot_shim {

size_t PoolMemConfigItem::size_bytes() const { return pool_ ? ItemSize(PayloadSize(*pool_)) : 0; }

fit::result<ErrorType> PoolMemConfigItem::AppendItems(DataZbi& zbi) const {
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
  return fit::ok();
}

}  // namespace boot_shim
