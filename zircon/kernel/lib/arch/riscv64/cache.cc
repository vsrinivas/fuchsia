// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/cache.h>

namespace arch {

GlobalCacheConsistencyContext::~GlobalCacheConsistencyContext() {
}

void GlobalCacheConsistencyContext::SyncRange(uintptr_t vaddr, size_t size) {
  if (possible_aliasing_) return;
}

}  // namespace arch

