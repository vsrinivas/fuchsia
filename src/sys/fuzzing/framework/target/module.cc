// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/target/module.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <third_party/modp_b64/modp_b64.h>

#include "src/sys/fuzzing/common/module.h"

namespace fuzzing {

Module::Module(uint8_t* counters, const uintptr_t* pcs, size_t num_pcs) {
  FX_CHECK(counters && pcs && num_pcs);
  if (auto status = counters_.Mirror(counters, num_pcs); status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to create module: " << zx_status_get_string(status);
  }
  // Make a position independent table from the PCs.
  auto pc_table = std::make_unique<ModulePC[]>(num_pcs);
  for (size_t i = 0; i < num_pcs; ++i) {
    pc_table[i].pc = pcs[i * 2] - pcs[0];
    pc_table[i].flags = pcs[i * 2 + 1];
  }
  // Double hash using both FNV1a and DJB2a to reduce the likelihood of collisions. We could use a
  // cryptographic hash here, but that introduces unwanted dependencies, and this is good enough.
  // The algorithms are taken from http://www.isthe.com/chongo/tech/comp/fnv/index.html and
  // http://www.cse.yorku.ca/~oz/hash.html.
  uint64_t fnv1a = 14695981039346656037ULL;
  uint64_t djb2a = 5381;
  auto* u8 = reinterpret_cast<uint8_t*>(pc_table.get());
  size_t size = num_pcs * sizeof(ModulePC);
  while (size-- > 0) {
    fnv1a = (fnv1a ^ *u8) * 1099511628211ULL;
    djb2a = ((djb2a << 5) + djb2a) ^ *u8;
    u8++;
  }
  legacy_id_ = Identifier{fnv1a, djb2a};
  // To squeeze as much data as possible into the null-terminated 32-character name limit, this
  // omits the most significant byte of each hash and all padding characters. Thus, the 7 bytes
  // hashes produce 10 characters each. Combined with 11 characters for the |target_id| (see |Share|
  // below), this gives a total string length of 31.
  char id[modp_b64_encode_len(sizeof(uint64_t) * 2)];
  auto len1 = modp_b64_encode(&id[0], reinterpret_cast<char*>(&fnv1a), sizeof(uint64_t) - 1);
  len1 -= 2;
  FX_DCHECK(id[len1] == '=');
  auto len2 = modp_b64_encode(&id[len1], reinterpret_cast<char*>(&djb2a), sizeof(uint64_t) - 1);
  len2 += len1 - 2;
  FX_DCHECK(id[len2] == '=');
  id_ = std::string(id, len2);
}

Module& Module::operator=(Module&& other) noexcept {
  legacy_id_ = other.legacy_id_;
  id_ = other.id_;
  counters_ = std::move(other.counters_);
  return *this;
}

zx_status_t Module::Share(uint64_t target_id, zx::vmo* out) const {
  if (auto status = counters_.Share(out); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to share module: " << zx_status_get_string(status);
    return status;
  }
  // As noted above, this trims the final padding character to fit the |target_id| into 11
  // characters. Keep this in sync with the decoding routines in engine/coverage-data.cc!
  char name[ZX_MAX_NAME_LEN];
  auto len = modp_b64_encode(name, reinterpret_cast<char*>(&target_id), sizeof(uint64_t));
  FX_CHECK(len != size_t(-1));
  FX_CHECK(name[len - 1] == '=');
  strncpy(&name[len - 1], id_.c_str(), sizeof(name) - (len - 1));
  if (auto status = out->set_property(ZX_PROP_NAME, name, sizeof(name)); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to set module ID: " << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
}

}  // namespace fuzzing
