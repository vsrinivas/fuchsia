// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/allocator.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/page-table/builder-interface.h>
#include <lib/uart/uart.h>

#include <ktl/algorithm.h>
#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/type_traits.h>
#include <phys/page-table.h>
#include <phys/stdio.h>

void MapUart(page_table::AddressSpaceBuilderInterface& builder, memalloc::Pool& pool) {
  // Meets the signature expected of uart::BasicIoProvider's constructor.
  auto mapper = [&pool, &builder ](uint64_t uart_mmio_base) -> volatile void* {
    const memalloc::MemRange* containing = pool.GetContainingRange(uart_mmio_base);
    if (!containing) {
      ZX_PANIC("UART registers not encoded among ZBI memory ranges");
    }

    uint64_t base = ktl::max(containing->addr, uart_mmio_base & ~(ZX_PAGE_SIZE - 1));
    uint64_t size = ktl::min(containing->end() - base, uint64_t{ZX_PAGE_SIZE});
    zx_status_t status = builder.MapRegion(page_table::Vaddr(base), page_table::Paddr(base), size,
                                           page_table::CacheAttributes::kDevice);
    if (status != ZX_OK) {
      ZX_PANIC("Failed to map in UART range");
    }
    return reinterpret_cast<volatile void*>(uart_mmio_base);
  };

  GetUartDriver().Visit([mapper = ktl::move(mapper)](auto&& driver) {
    using config_type = typename ktl::decay_t<decltype(driver.uart())>::config_type;
    if constexpr (ktl::is_same_v<config_type, dcfg_simple_t> ||
                  ktl::is_same_v<config_type, dcfg_soc_uart_t>) {
      driver.io() = uart::BasicIoProvider<config_type>{
          driver.uart().config(),
          driver.uart().pio_size(),
          ktl::move(mapper),
      };
    }
    // Extend as more MMIO config types surface...
  });
}

ktl::byte* AllocationMemoryManager::Allocate(size_t size, size_t alignment) {
  auto result = pool_.Allocate(memalloc::Type::kIdentityPageTables, size, alignment);
  if (result.is_error()) {
    return nullptr;
  }
  return reinterpret_cast<ktl::byte*>(static_cast<uintptr_t>(result.value()));
}
