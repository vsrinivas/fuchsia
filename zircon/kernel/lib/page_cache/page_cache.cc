// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/page_cache.h"

#include <lib/counters.h>

#include <new>

#include <arch/defines.h>
#include <kernel/percpu.h>

KCOUNTER(page_cache_hit_pages, "cache.page.hit")
KCOUNTER(page_cache_miss_pages, "cache.page.missed")
KCOUNTER(page_cache_refill_pages, "cache.page.refilled")
KCOUNTER(page_cache_return_pages, "cache.page.returned")
KCOUNTER(page_cache_free_pages, "cache.page.freed")

namespace page_cache {

zx::result<PageCache> PageCache::Create(size_t reserve_pages) {
  const size_t cpu_count = percpu::processor_count();
  DEBUG_ASSERT(cpu_count != 0);

  fbl::AllocChecker alloc_checker;
  ktl::unique_ptr<CpuCache[]> entries{new (&alloc_checker) CpuCache[cpu_count]};
  if (!alloc_checker.check()) {
    return zx::error_result(ZX_ERR_NO_MEMORY);
  }
  DEBUG_ASSERT(((MAX_CACHE_LINE - 1) & reinterpret_cast<uintptr_t>(entries.get())) == 0);
  return zx::ok(PageCache{reserve_pages, ktl::move(entries)});
}

void PageCache::CountHitPages(size_t page_count) {
  page_cache_hit_pages.Add(static_cast<int64_t>(page_count));
}

void PageCache::CountMissPages(size_t page_count) {
  page_cache_miss_pages.Add(static_cast<int64_t>(page_count));
}

void PageCache::CountRefillPages(size_t page_count) {
  page_cache_refill_pages.Add(static_cast<int64_t>(page_count));
}

void PageCache::CountReturnPages(size_t page_count) {
  page_cache_return_pages.Add(static_cast<int64_t>(page_count));
}

void PageCache::CountFreePages(size_t page_count) {
  page_cache_free_pages.Add(static_cast<int64_t>(page_count));
}

}  // namespace page_cache
