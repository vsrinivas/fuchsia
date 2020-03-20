// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VMO_STORE_STORAGE_TYPES_H_
#define SRC_LIB_VMO_STORE_STORAGE_TYPES_H_

#include <random>

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_hash_table.h>

#include "growable_slab.h"
#include "stored_vmo.h"

namespace vmo_store {

// Defines the contract of a base storage class that can be used with `VmoStore`.
// `Key` is the type of key used to address VMOs in the store.
// `Meta` is optional user metadata associated with the `StoredVmo`s kept by the store.
template <typename Key, typename Meta = void>
class AbstractStorage {
 public:
  virtual ~AbstractStorage() = default;
  // Reserves `capacity` lots on this store.
  virtual zx_status_t Reserve(size_t capacity) = 0;
  // Insert `vmo` at `key`.
  // Must return `ZX_ERR_ALREADY_EXISTS` if `key` is already in use.
  virtual zx_status_t Insert(Key key, StoredVmo<Meta>&& vmo) = 0;
  // Allocates an unused key and associates `vmo` with it, returning the new key on success.
  virtual fit::optional<Key> Push(StoredVmo<Meta>&& vmo) = 0;
  // Get the `StoredVmo` associated with `key`. Returns `nullptr` if `key` doesn't match a stored
  // VMO.
  virtual StoredVmo<Meta>* Get(const Key& key) = 0;
  // Erases the VMO referenced by `key`.
  // Returns `true` if `key` pointed to an existing `StoredVmo`.
  virtual bool Erase(Key key) = 0;
  // Returns the number of registered `StoredVmo`s in this store.
  virtual size_t count() const = 0;

  // Implement `is_full` for types that will not automatically grow so users can be notified that
  // the container needs to grow.
  virtual bool is_full() const { return false; }
};

// A storage base for VmoStore that uses a GrowableSlab backing.
// SlabStorage is always constructed with a 0 capacity and must be manually grown by calling
// `Reserve`.
// SlabStorage is optimally suited for narrow and tight (non-sparse) key sets. It guarantees O(1)
// worst-case time-complexity on `Push`, `Get`, `Erase`, and `Insert`, but O(n) on `Reserve`.
template <typename _Key, typename _Meta = void>
class SlabStorage : public AbstractStorage<_Key, _Meta> {
 public:
  using Key = _Key;
  using Meta = _Meta;
  using Item = StoredVmo<Meta>;
  SlabStorage() = default;

  inline zx_status_t Reserve(size_t capacity) override {
    return slab_.GrowTo(static_cast<Key>(capacity));
  }

  inline zx_status_t Insert(Key key, Item&& vmo) override {
    return slab_.Insert(key, std::move(vmo));
  }

  inline fit::optional<Key> Push(Item&& vmo) override { return slab_.Push(std::move(vmo)); }

  inline Item* Get(const Key& key) override { return slab_.Get(key); }

  bool Erase(Key key) override { return slab_.Erase(key).has_value(); }

  inline size_t count() const override { return slab_.count(); }

  inline bool is_full() const override { return slab_.free() == 0; }

 private:
  GrowableSlab<Item, Key> slab_;
};

// A storage base for VmoStore that uses an fbl::HashTable backing.
// The hash table nodes are std::unique_ptrs that are fallibly allocated on insertion.
//
// Users should consider `HashTableStorage` over `SlabStorage` if any of the following are true:
// - The `Insert` API is expected to be used more than the `Push` API. `SlabStorage` is better
// suited than `HashTableStorage` to issue keys, but if the keys are always informed (i.e. the
// `RegisterWithKey` API on `VmoStore` is used), then `HashTableStorage` might be a better option.
// - There are no guarantees over the key space or the keys are not expected to be tightly packed.
// `SlabStorage`'s keys are simply an index in an internal vector. If there are no guarantees over
// the size or sparseness of the key set that you expect to use, `HashTableStorage` might be a
// better option.
// - Upfront memory allocation and memory reuse are not necessary.
// - The application can pay the cost of a multiplication and a mod on `Get` and absorb the O(n)
// worst-case scenario that comes with hash tables, as opposed to the stronger O(1) guarantees of
// `SlabStorage`.
//
// NOTE: This class only supports integer types as keys for simplicity. It can be expanded
// with more complex type traits derived from fbl::HashTable, but there's no immediate need for it.
template <typename _Key, typename _Meta = void>
class HashTableStorage : public AbstractStorage<_Key, _Meta> {
 public:
  using Key = _Key;
  using Meta = _Meta;
  using Item = StoredVmo<Meta>;

  static_assert(std::numeric_limits<Key>::is_integer);
  static_assert(!std::numeric_limits<Key>::is_signed);

  explicit HashTableStorage(uint32_t random_seed = std::default_random_engine::default_seed)
      : rnd_distro_(0, std::numeric_limits<Key>::max()), rnd_(random_seed) {}

  HashTableStorage(HashTableStorage&& other)
      : rnd_distro_(std::move(other.rnd_distro_)),
        rnd_(std::move(other.rnd_)),
        table_(std::move(other.table_)) {}

  class HashTableVmo : public fbl::SinglyLinkedListable<std::unique_ptr<HashTableVmo>> {
   public:
    explicit HashTableVmo(Key key, Item&& vmo) : key_(key), vmo_(std::move(vmo)) {}

    // Our simple hash function just multiplies by a big prime, the DefaultHashTraits in the hash
    // table will mod by the number of buckets.
    static size_t GetHash(Key key) { return static_cast<size_t>(key) * 0xcf2fd713; }

    // Needed to comply with `DefaultKeyedObjectTraits`.
    Key GetKey() const { return key_; }

   protected:
    friend HashTableStorage;
    Key key_;
    Item vmo_;
  };

  zx_status_t Reserve(size_t capacity) override { return ZX_OK; }

  zx_status_t Insert(Key key, Item&& vmo) override {
    if (table_.find(key) != table_.end()) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    fbl::AllocChecker ac;
    std::unique_ptr<HashTableVmo> holder(new (&ac) HashTableVmo(key, std::move(vmo)));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    table_.insert(std::move(holder));
    return ZX_OK;
  }

  fit::optional<Key> Push(Item&& vmo) override {
    Key key = rnd_distro_(rnd_);
    auto search = table_.find(key);
    while (search != table_.end()) {
      key = rnd_distro_(rnd_);
      search = table_.find(key);
    }
    if (Insert(key, std::move(vmo)) != ZX_OK) {
      return fit::nullopt;
    }
    return key;
  }

  Item* Get(const Key& key) override {
    auto search = table_.find(key);
    if (search == table_.end()) {
      return nullptr;
    }
    return &search->vmo_;
  }

  inline bool Erase(Key key) override { return table_.erase(key) != nullptr; }

  inline size_t count() const override { return table_.size(); }

  inline bool is_full() const override { return false; }

 private:
  std::uniform_int_distribution<Key> rnd_distro_;
  std::default_random_engine rnd_;
  fbl::HashTable<Key, std::unique_ptr<HashTableVmo>> table_;
};

// Provides a BackingStore for `VmoStore` that uses any implementer of `AbstractStorage`.
// This type of BackingStore can be used if the static dispatch option provided by `VmoStore` is not
// feasible or desirable, such as providing C-bindings for `VmoStore`, for example.
// `DynamicDispatchStorage` can be used to have different backing stores decided at runtime, at the
// cost of having dynamic dispatch method calls, which can be slower than static dispatch.
//
// For example, we can have two different backing store implementations and we can decide between
// which to use at runtime, using the same `VmoStore` type:
//
// class Foo: public AbstractStorage<uint32_t> { /* ... */ }
// class Bar: public AbstractStorage<uint32_t> { /* ... */ }
// using MyVmoStore = VmoStore<DynamicDispatchStorage<uint32_t>>;
// void MyFunction(bool foo_or_bar) {
//    MyVmoStore my_store(/* ... */,
//              std::unique_ptr<AbstractStorage>(foo_or_bar ? new Foo() : new Bar());
// }
template <typename _Key, typename _Meta = void>
class DynamicDispatchStorage {
 public:
  using Key = _Key;
  using Meta = _Meta;
  using Item = StoredVmo<Meta>;
  using Base = AbstractStorage<Key, Meta>;
  using BasePtr = std::unique_ptr<Base>;

  explicit DynamicDispatchStorage(BasePtr impl) : impl_(std::move(impl)) {}

  inline zx_status_t Reserve(size_t capacity) { return impl_->Reserve(capacity); }
  inline zx_status_t Insert(Key key, Item&& vmo) {
    return impl_->Insert(std::move(key), std::move(vmo));
  }
  inline fit::optional<Key> Push(Item&& vmo) { return impl_->Push(std::move(vmo)); }
  inline Item* Get(const Key& key) { return impl_->Get(key); }
  bool Erase(Key key) { return impl_->Erase(std::move(key)); }
  inline size_t count() const { return impl_->count(); }
  inline bool is_full() const { return impl_->is_full(); }

 private:
  BasePtr impl_;
};

}  // namespace vmo_store

#endif  // SRC_LIB_VMO_STORE_STORAGE_TYPES_H_
