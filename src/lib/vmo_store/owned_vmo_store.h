// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_OWNED_VMO_STORE_H_
#define SRC_LIB_VMO_STORE_OWNED_VMO_STORE_H_

#include <lib/zx/vmo.h>

#include "vmo_store.h"

namespace vmo_store {

// A `VmoStore` that may only be accessed through a `RegistrationAgent`.
//
// `OwnedVmoStore` composes with `VmoStore` to provide a wrapper that only allows access to the
// registered VMOs through the creation of `RegistrationAgent`s.
//
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
//   using MyOwnedVmoStore = OwnedVmoStore<HashTableStorage<MyKey, MyMeta>>;
//   MyOwnedVmoStore store(Options{...});
//
//   // Declare a registration agent for it. The agent provides a view into the store.
//   MyOwnedVmoStore::RegistrationAgent agent = store.CreateRegistrationAgent();
//
//   // Now let's register, retrieve, and unregister a `zx::vmo` obtained through `GetVmo()`.
//   // The second argument to `Register` is our user metadata.
//   zx::result result = agent.Register(GetVmo(), "my first VMO");
//   size_t key = result.take_value();
//   auto * my_registered_vmo = agent.GetVmo(key);
//
//   // Print metadata associated with VMO.
//   std::cout << "Got Vmo called " << my_registered_vmo->meta() << std::endl;
//   // see `stored_vmo.h` for other `StoredVmo` methods, like retrieving mapped or pinned memory.
//
//   // A different agent will not have access to the same VMO using the key.
//   MyVmoStore::RegistrationAgent other_agent = store.CreateRegistrationAgent();
//   ZX_ASSERT(other_agent.GetVmo(key) == nullptr, "no soup for other_agent");
//
//   // Finally, unregister the VMO, which will discard the VMO handle along with any mapping or
//   // pinning. Destroying the agent without unregistering all its VMOs will cause a crash.
//   agent.Unregister(key);
// ```
template <typename Backing>
class OwnedVmoStore : public VmoStoreBase<VmoStore<Backing>> {
 public:
  template <typename... StoreArgs>
  explicit OwnedVmoStore(StoreArgs... store_args)
      : VmoStoreBase<VmoStore<Backing>>(std::forward<StoreArgs>(store_args)...) {}

  class RegistrationAgent;

  // Creates a `RegistrationAgent` attached to this `OwnedVmoStore`.
  // The returned agent may not outlive this `OwnedVmoStore`.
  RegistrationAgent CreateRegistrationAgent() { return RegistrationAgent(this); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(OwnedVmoStore);

  // An agent which owns VMOs in an `OwnedVmoStore`.
  //
  // `RegistrationAgent` serves as the registration point for VMOs stored in an `OwnedVmoStore`.
  // A `RegistrationAgent` provides runtime guardrails so that multiple agents can use the same
  // store, but they can't access each other's VMOs.
  //
  // Destroying a `RegistrationAgent` without first unregistering all the VMOs that it registered is
  // invalid, and causes the program to crash.
  //
  // Note that `RegistrationAgent` does not provide any thread-safety guarantees, users must provide
  // their own locking mechanisms to ensure that different `RegistrationAgent`s can't compete across
  // threads, taking into account the chosen `Backing` method for the `OwnedVmoStore`.
  class RegistrationAgent : internal::VmoOwner {
   public:
    using VmoStore = ::vmo_store::VmoStore<Backing>;
    using StoredVmo = typename VmoStore::StoredVmo;

    // Creates a `RegistrationAgent` attached to `store`.
    //
    // `RegistrationAgent` may not outlive `store`.
    explicit RegistrationAgent(OwnedVmoStore<Backing>* store)
        : store_(&store->impl_), registration_count_(0) {}
    ~RegistrationAgent() {
      size_t registered = registration_count_;
      ZX_ASSERT_MSG(registered == 0,
                    "Attempted to destroy a RegistrationAgent with %ld registered VMOs",
                    registered);
    }

    // Same as `VmoStore::Register`, but the registered VMO is only accessible through this
    // `RegistrationAgent`.
    template <typename... MetaArgs>
    zx::result<typename VmoStore::Key> Register(zx::vmo vmo, MetaArgs... vmo_args) {
      auto result =
          store_->Register(CreateStore(std::move(vmo), std::forward<MetaArgs>(vmo_args)...));
      if (result.is_ok()) {
        registration_count_++;
      }
      return result;
    }

    // Same as `VmoStore::RegisterWithKey`, but the registered VMO is only accessible through this
    // `RegistrationAgent`.
    template <typename... MetaArgs>
    zx_status_t RegisterWithKey(typename VmoStore::Key key, zx::vmo vmo, MetaArgs... vmo_args) {
      zx_status_t result = store_->RegisterWithKey(
          std::move(key), CreateStore(std::move(vmo), std::forward<MetaArgs>(vmo_args)...));
      if (result == ZX_OK) {
        registration_count_++;
      }
      return result;
    }

    // Same as `VmoStore::Unregister`, but unregistration fails with `ZX_ERR_ACCESS_DENIED` if the
    // VMO was not initially registered by this `RegistrationAgent`.
    zx::result<zx::vmo> Unregister(typename VmoStore::Key key) {
      auto* vmo = store_->GetVmo(key);
      if (vmo && GetOwner(*vmo) != this) {
        return zx::error(ZX_ERR_ACCESS_DENIED);
      }
      auto result = store_->Unregister(std::move(key));
      if (result.is_ok()) {
        registration_count_--;
      }
      return result;
    }

    // Same as `VmoStore::GetVmo`, but only returns non-null if the VMO referenced by `key` was
    // originally registered by this `RegistrationAgent`.
    StoredVmo* GetVmo(const typename VmoStore::Key& key) {
      auto* vmo = store_->GetVmo(key);
      if (vmo && GetOwner(*vmo) == this) {
        return vmo;
      }
      return nullptr;
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(RegistrationAgent);

   private:
    template <typename... MetaArgs>
    StoredVmo CreateStore(zx::vmo vmo, MetaArgs... vmo_args) {
      StoredVmo stored_vmo(std::move(vmo), std::forward<MetaArgs>(vmo_args)...);
      SetOwner(&stored_vmo, this);
      return stored_vmo;
    }

    // Pointer to parent store, not owned.
    VmoStore* store_;
    std::atomic_size_t registration_count_;
  };
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_OWNED_VMO_STORE_H_
