// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_STORED_VMO_H_
#define SRC_LIB_VMO_STORE_STORED_VMO_H_

#include <lib/zx/vmo.h>

#include <utility>

namespace vmo_store {

namespace internal {
template <typename M>
struct MetaStorage {
  explicit MetaStorage(M&& value) : value(std::move(value)) {}
  M value;
};
template <>
struct MetaStorage<void> {};
}  // namespace internal

// A VMO stored in a `VmoStore`.
// A `StoredVmo` may have optional `Meta` user metadata associated with it.
template <typename Meta>
class StoredVmo {
 public:
  template <typename = std::enable_if<std::is_void<Meta>::value>>
  explicit StoredVmo(zx::vmo vmo) : vmo_(std::move(vmo)) {}

  template <typename M = Meta, typename = std::enable_if<!std::is_void<Meta>::value>>
  StoredVmo(zx::vmo vmo, M meta) : vmo_(std::move(vmo)), meta_(std::move(meta)) {}

  template <typename M = Meta, typename = std::enable_if<!std::is_void<Meta>::value>>
  M& meta() {
    return meta_.value;
  }

  // Get an unowned handle to the VMO.
  zx::unowned_vmo vmo() { return zx::unowned_vmo(vmo_); }

  StoredVmo(StoredVmo&& other) noexcept = default;
  StoredVmo& operator=(StoredVmo&& other) noexcept = default;

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(StoredVmo);

 private:
  zx::vmo vmo_;
  internal::MetaStorage<Meta> meta_;
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_STORED_VMO_H_
