// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_VMO_STORE_H_
#define SRC_LIB_VMO_STORE_VMO_STORE_H_

#include <lib/stdcompat/optional.h>
#include <lib/zx/result.h>
#include <zircon/status.h>

#include <memory>

#include <fbl/alloc_checker.h>

#include "storage_types.h"

namespace vmo_store {

// `VmoStore` pinning options.
struct PinOptions {
  // The BTI used for pinning.
  // Note that `VmoStore` does *not* take ownership of the BTI handle. It is the caller's
  // responsibility to ensure the BTI handle is valid.
  zx::unowned_bti bti;
  // Options passed to zx_bti_pin. See `StoredVmo::Map` for more details.
  uint32_t bti_pin_options;
  // Index pinned pages for fast lookup. See `StoredVmo::Map` for more details.
  bool index;
};

struct MapOptions {
  // Options passed to `zx_vmar_map`.
  zx_vm_option_t vm_option;
  // Pointer to `VmarManager`. If null, the root `vmar` will be used.
  fbl::RefPtr<fzl::VmarManager> vmar;
};

// `VmoStore` options controlling mapping and pinning behavior.
struct Options {
  // If provided, `VmoStore` will attempt to map stored VMOs.
  cpp17::optional<MapOptions> map;
  // If provided, `VmoStore` will attempt to pin stored VMOs.
  cpp17::optional<PinOptions> pin;
};

// A base class used to compose `VmoStore`s.
//
// Users should not use `VmoStoreBase` directly, use `VmoStore` or `OwnedVmoStore` instead.
//
// `Impl` is a base implementation that is either `AbstractStorage` or
// `VmoStoreBase<AbstractStorage>`.
template <typename Impl>
class VmoStoreBase {
 public:
  static_assert(internal::has_key_v<Impl>, "Backing must define a Key type");
  static_assert(internal::has_meta_v<Impl>, "Backing must define a Meta type");
  // The key type used to reference VMOs.
  using Key = typename Impl::Key;
  // User metadata associated with every registered VMO.
  using Meta = typename Impl::Meta;
  using StoredVmo = ::vmo_store::StoredVmo<Meta>;

  // Reserves `capacity` slots on the underlying store.
  // Stores that grow automatically may chose to pre-allocate memory on `Reserve`.
  // Stores that do not grow automatically will only increase their memory consumption upon
  // `Reserve` being called.
  zx_status_t Reserve(size_t capacity) { return impl_.Reserve(capacity); }

  // Returns the number of registered VMOs.
  size_t count() const { return impl_.count(); }
  // Returns `true` if the backing store is full.
  // Stores that grow automatically will never report that they're full.
  bool is_full() const { return impl_.is_full(); }

 protected:
  template <typename... StoreArgs>
  VmoStoreBase(StoreArgs... store_args) : impl_(std::forward<StoreArgs>(store_args)...) {}
  Impl impl_;
};

// A data structure that keeps track of registered VMOs using a `Backing` storage type.
// `VmoStore` keeps track of registered VMOs and performs common mapping and pinning operations,
// providing common operations used in VMO pre-registration on Banjo and FIDL APIs.
// This structure is not thread-safe. Users must provide their own thread-safety accounting for the
// chosen `Backing` format.
// `Backing` is the data structure used to store the registered VMOs. It must implement
// `AbstractStorage`.
//
// Example usage:
// ```
//   using namespace vmo_store;
//   // Declaring our types first.
//   // `MyKey` is the key type that is used to register and retrieve VMOs from a VmoStore.
//   using MyKey = size_t;
//   // `MyMeta` is extra user metadata associated with every stored VMO (can be `void`).
//   using MyMeta = std::string;
//   // Declare our store alias, we're using `HashTableStorage` in this example.
//   // See `storage_types.h` for other Backing storage types.
//   using MyVmoStore = VmoStore<HashTableStorage<MyKey, MyMeta>>;
//   MyVmoStore store(Options{...});
//
//   // Now let's register, retrieve, and unregister a `zx::vmo` obtained through `GetVmo()`.
//   // The second argument to `Register` is our user metadata.
//   zx::result<size_t> result = store.Register(GetVmo(), "my first VMO");
//   size_t key = result.take_value();
//   auto * my_registered_vmo = store.GetVmo(key);
//
//   // Print metadata associated with VMO.
//   std::cout << "Got Vmo called " << my_registered_vmo->meta() << std::endl;
//   // see `stored_vmo.h` for other `StoredVmo` methods, like retrieving mapped or pinned memory.
//
//   // Finally, unregister the VMO, which will discard the VMO handle along with any mapping or
//   // pinning.
//   store.Unregister(key);
// ```
//
// See `OwnedVmoStore` for an alternative API where registration happens through an ownership agent.
template <typename Backing>
class VmoStore : public VmoStoreBase<Backing> {
 public:
  using typename VmoStoreBase<Backing>::Key;
  using typename VmoStoreBase<Backing>::Meta;
  using typename VmoStoreBase<Backing>::StoredVmo;

  static_assert(
      internal::is_abstract_storage<Backing>,
      "Backing must implement AbstractStorage, see storage_types.h for build in storage classes");

  template <typename... StoreArgs>
  explicit VmoStore(Options options, StoreArgs... store_args)
      : VmoStoreBase<Backing>(std::forward<StoreArgs>(store_args)...),
        options_(std::move(options)) {}

  // Registers a VMO with this store, returning the key used to access that VMO on success.
  template <typename... MetaArgs>
  zx::result<Key> Register(zx::vmo vmo, MetaArgs... vmo_args) {
    return Register(StoredVmo(std::move(vmo), std::forward<MetaArgs>(vmo_args)...));
  }

  zx::result<Key> Register(StoredVmo vmo) {
    zx_status_t status = PrepareStore(&vmo);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    auto key = this->impl_.Push(std::move(vmo));
    if (!key.has_value()) {
      return zx::error(ZX_ERR_NO_RESOURCES);
    }
    return zx::ok(std::move(*key));
  }

  // Registers a VMO with this store using the provided `key`.
  template <typename... MetaArgs>
  zx_status_t RegisterWithKey(Key key, zx::vmo vmo, MetaArgs... vmo_args) {
    return RegisterWithKey(std::move(key),
                           StoredVmo(std::move(vmo), std::forward<MetaArgs>(vmo_args)...));
  }

  zx_status_t RegisterWithKey(Key key, StoredVmo vmo) {
    zx_status_t status = PrepareStore(&vmo);
    if (status != ZX_OK) {
      return status;
    }
    return this->impl_.Insert(std::move(key), std::move(vmo));
  }

  // Unregisters the VMO at `key`.
  // All the mapping and pinning handles will be dropped, and the VMO will be
  // returned to the caller.
  // Returns `ZX_ERR_NOT_FOUND` if `key` does not point to a registered VMO.
  zx::result<zx::vmo> Unregister(Key key) {
    cpp17::optional<StoredVmo> vmo = this->impl_.Extract(key);
    if (vmo) {
      return zx::ok(std::move(vmo->take_vmo()));
    }
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  // Gets an _unowned_ pointer to the `StoredVmo` referenced by `key`.
  // Returns `nullptr` if `key` does not point to a register VMO.
  StoredVmo* GetVmo(const Key& key) { return this->impl_.Get(key); }

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VmoStore);

 private:
  zx_status_t PrepareStore(StoredVmo* vmo) {
    if (!vmo->vmo()->is_valid()) {
      return ZX_ERR_BAD_HANDLE;
    }
    zx_status_t status;
    if (options_.map) {
      const auto& map_options = *options_.map;
      status = vmo->Map(map_options.vm_option, map_options.vmar);
      if (status != ZX_OK) {
        return status;
      }
    }
    if (options_.pin) {
      const auto& pin_options = *options_.pin;
      status = vmo->Pin(*pin_options.bti, pin_options.bti_pin_options, pin_options.index);
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

  Options options_;
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_VMO_STORE_H_
