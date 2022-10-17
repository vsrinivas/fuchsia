// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/buffer_chain.h"

#include <lib/boot-options/boot-options.h>

#include <lk/init.h>

// Makes a const char* look like a user_in_ptr<const char>.
//
// Sometimes we need to copy data from kernel space. KernelPtrAdapter allows us to implement the
// copy logic once for both const char* and user_in_ptr<const char>.
class KernelPtrAdapter {
 public:
  explicit KernelPtrAdapter(const char* p) : p_(p) {}
  zx_status_t copy_array_from_user(char* dst, size_t count) const {
    memcpy(dst, p_, count);
    return ZX_OK;
  }
  KernelPtrAdapter byte_offset(size_t offset) const {
    return KernelPtrAdapter(reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(p_) + offset));
  }

 private:
  const char* p_;
};

zx_status_t BufferChain::AppendKernel(const char* src, size_t size) {
  return AppendCommon(KernelPtrAdapter(src), size);
}

template zx_status_t BufferChain::AppendCommon(user_in_ptr<const char> src, size_t size);

void BufferChain::InitializePageCache(uint32_t /*level*/) {
  zx::result<page_cache::PageCache> result =
      page_cache::PageCache::Create(gBootOptions->bufferchain_reserve_pages);
  ASSERT(result.is_ok());
  page_cache_ = ktl::move(result.value());
}

// Initialize the cache after the percpu data structures are initialized.
LK_INIT_HOOK(buffer_chain_cache_init, BufferChain::InitializePageCache, LK_INIT_LEVEL_KERNEL + 1)
