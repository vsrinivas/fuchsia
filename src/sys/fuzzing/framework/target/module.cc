// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/target/module.h"

#include <lib/syslog/cpp/macros.h>

namespace fuzzing {
namespace {

constexpr uint64_t kFnv64Prime = 1099511628211ULL;
constexpr uint64_t kFnv64OffsetBasis = 14695981039346656037ULL;

}  //  namespace

Identifier Module::Identify(const uintptr_t* pcs, size_t num_pcs) {
  // Make a position independent table from the PCs.
  auto pc_table = std::make_unique<PC[]>(num_pcs);
  for (size_t i = 0; i < num_pcs; ++i) {
    pc_table[i].pc = pcs[i * 2] - pcs[0];
    pc_table[i].flags = pcs[i * 2 + 1];
  }

  // Double hash using both FNV1 and FNV1a to reduce the likelihood of collisions. We could use a
  // cryptographic hash here, but that introduces unwanted dependencies, and this is good enough.
  // The algorithms are taken from http://www.isthe.com/chongo/tech/comp/fnv/index.html
  Identifier id = {kFnv64OffsetBasis, kFnv64OffsetBasis};
  auto* u8 = reinterpret_cast<uint8_t*>(pc_table.get());
  size_t size = num_pcs * sizeof(PC);
  while (size-- > 0) {
    // FNV1
    id[0] *= kFnv64Prime;
    id[0] ^= *u8;
    // FNV1a
    id[1] ^= *u8++;
    id[1] *= kFnv64Prime;
  }
  return id;
}

Module::Module(uint8_t* counters, const uintptr_t* pcs, size_t num_pcs) {
  FX_CHECK(counters && pcs && num_pcs);
  counters_.Mirror(counters, num_pcs);
  id_ = Module::Identify(pcs, num_pcs);
}

Module& Module::operator=(Module&& other) noexcept {
  id_ = other.id_;
  counters_ = std::move(other.counters_);
  return *this;
}

}  // namespace fuzzing
