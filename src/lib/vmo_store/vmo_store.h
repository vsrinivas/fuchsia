// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_VMO_STORE_H_
#define SRC_LIB_VMO_STORE_VMO_STORE_H_

#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/alloc_checker.h>

#include "storage_types.h"

namespace vmo_store {

// Automatically maps every registered VMO to virtual memory.
constexpr uint32_t kOptionMapVmo = (1u << 0u);
// Automatically pins every registered VMO using the provided BTI.
constexpr uint32_t kOptionPinVmo = (1u << 1u);

// A data structure that keeps track of registered VMOs using a `BackingStore`.
// `VmoStore` keeps track of registered VMOs and performs common mapping and pinning operations,
// providing common operations used in VMO pre-registration on Banjo and FIL APIs.
// This structure is not thread-safe. Users must provide their own thread-safety accounting for the
// chosen `BackingStore` format.
template <typename BackingStore>
class VmoStore {
 public:
  // The key type used to reference VMOs.
  using Key = typename BackingStore::Key;
  // User metadata associated with every registered VMO.
  using Meta = typename BackingStore::Meta;
  using StoredVmo = ::vmo_store::StoredVmo<Meta>;

  template <typename... StoreArgs>
  explicit VmoStore(uint32_t options, StoreArgs... store_args)
      : store_(store_args...),
        options_(options){

        };

  // Reserves `capacity` slots on the underlying store.
  // Stores that grow automatically may chose to pre-allocate memory on `Reserve`.
  // Stores that do not grow automatically will only increase their memory consumption upon
  // `Reserve` being called.
  zx_status_t Reserve(size_t capacity) { return store_.Reserve(capacity); }

  // Returns the number of registered VMOs.
  size_t count() const { return store_.count(); }
  // Returns `true` if the backing store is full.
  // Stores that grow automatically will never report that they're full.
  bool is_full() const { return store_.is_full(); }

  // Registers a VMO with this store, returning the key used to access that VMO on success.
  template <typename... MetaArgs>
  fit::result<Key, zx_status_t> Register(zx::vmo vmo, MetaArgs... vmo_args) {
    return Register(StoredVmo(std::move(vmo), vmo_args...));
  }

  fit::result<Key, zx_status_t> Register(StoredVmo vmo) {
    // TODO map and pin the VMO.
    auto key = store_.Push(std::move(vmo));
    if (!key.has_value()) {
      return fit::error(ZX_ERR_NO_RESOURCES);
    }
    return fit::ok(std::move(*key));
  }

  // Registers a VMO with this store using the provided `key`.
  template <typename... MetaArgs>
  zx_status_t RegisterWithKey(Key key, zx::vmo vmo, MetaArgs... vmo_args) {
    return RegisterWithKey(std::move(key), StoredVmo(std::move(vmo), vmo_args...));
  }

  zx_status_t RegisterWithKey(Key key, StoredVmo vmo) {
    // TODO map and pin the VMO.
    return store_.Insert(std::move(key), std::move(vmo));
  }

  // Unregisters the VMO at `key`.
  // The VMO handle will be dropped, alongside all the mapping and pinning handles.
  // Returns `ZX_ERR_NOT_FOUND` if `key` does not point to a registered VMO.
  zx_status_t Unregister(Key key) {
    if (!store_.Erase(key)) {
      return ZX_ERR_NOT_FOUND;
    }
    return ZX_OK;
  }

  // Gets an _unowned_ pointer to the `StoredVmo` referenced by `key`.
  // Returns `nullptr` if `key` does not point to a register VMO.
  StoredVmo* GetVmo(Key key) { return store_.Get(key); }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmoStore);

 private:
  BackingStore store_;
  uint32_t options_;
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_VMO_STORE_H_
