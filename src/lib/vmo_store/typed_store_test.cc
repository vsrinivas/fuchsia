// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <unordered_map>

#include <gtest/gtest.h>

#include "vmo_store.h"

namespace vmo_store {
namespace testing {

#define ASSERT_RESULT(result) \
  ASSERT_TRUE(result.is_ok()) << "Unexpected error result: " << zx_status_get_string(result.error())
#define ASSERT_OK(v) ASSERT_EQ(v, ZX_OK)

namespace {

zx::vmo MakeVmo() {
  zx::vmo vmo;
  EXPECT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo), ZX_OK);
  return vmo;
}

template <typename M>
StoredVmo<M> MakeStoredVmoHelper(zx::vmo vmo, uint64_t v) {
  return StoredVmo<M>(std::move(vmo), static_cast<M>(v));
}

template <>
StoredVmo<void> MakeStoredVmoHelper(zx::vmo vmo, uint64_t) {
  return StoredVmo<void>(std::move(vmo));
}

template <typename M>
void CompareMeta(StoredVmo<M>* vmo, uint64_t compare) {
  ASSERT_EQ(vmo->meta(), static_cast<M>(compare));
}

template <>
void CompareMeta(StoredVmo<void>* vmo, uint64_t compare) {}

template <typename K>
K MakeKey(uint64_t key) {
  return key;
}

template <>
std::string MakeKey(uint64_t key) {
  std::stringstream ss;
  ss << key;
  return ss.str();
}

}  // namespace

// An implementation of AbstractStorage to test the dynamic dispatch backing store.
// Also proves that keys may be non-integral values.
class UnorderedMapStorage : public AbstractStorage<std::string, int32_t> {
 public:
  zx_status_t Reserve(size_t capacity) override { return ZX_OK; }

  zx_status_t Insert(std::string key, StoredVmo<int32_t>&& vmo) override {
    if (map_.find(key) != map_.end()) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    map_.emplace(key, std::move(vmo));
    return ZX_OK;
  }

  fit::optional<std::string> Push(StoredVmo<int32_t>&& vmo) override {
    while (map_.find(auto_keys_) != map_.end()) {
      auto_keys_ += "a";
    }
    map_.emplace(auto_keys_, std::move(vmo));
    return auto_keys_;
  }

  StoredVmo<int32_t>* Get(const std::string& key) override {
    auto srch = map_.find(key);
    if (srch == map_.end()) {
      return nullptr;
    }
    return &srch->second;
  }

  bool Erase(std::string key) override { return map_.erase(key) != 0; }

  size_t count() const override { return map_.size(); }

 private:
  std::unordered_map<std::string, StoredVmo<int32_t>> map_;
  std::string auto_keys_ = "a";
};

class TestDynamicStorage : public DynamicDispatchStorage<std::string, int32_t> {
 public:
  TestDynamicStorage()
      : DynamicDispatchStorage(DynamicDispatchStorage::BasePtr(new UnorderedMapStorage)) {}
};

template <typename T>
class TypedStorageTest : public ::testing::Test {
 public:
  using VmoStore = ::vmo_store::VmoStore<T>;
  static constexpr size_t kStorageCapacity = 16;

  TypedStorageTest() : store_(Options()) {}

  void SetUp() override { ASSERT_OK(store_.Reserve(kStorageCapacity)); }

  static typename VmoStore::StoredVmo MakeStoredVmo(uint64_t meta = 0) {
    return MakeStoredVmoHelper<typename VmoStore::Meta>(MakeVmo(), meta);
  }

  VmoStore store_;
};

using TestTypes = ::testing::Types<SlabStorage<uint64_t, void>, SlabStorage<uint64_t, int32_t>,
                                   SlabStorage<uint8_t>, HashTableStorage<uint64_t, void>,
                                   HashTableStorage<uint64_t, int32_t>, HashTableStorage<uint8_t>,
                                   TestDynamicStorage>;

TYPED_TEST_SUITE(TypedStorageTest, TestTypes);

TYPED_TEST(TypedStorageTest, BasicStoreOperations) {
  auto& store = this->store_;
  auto vmo = TestFixture::MakeStoredVmo(1);
  auto vmo1 = vmo.vmo();
  auto result = store.Register(std::move(vmo));
  ASSERT_RESULT(result);
  auto k1 = result.take_value();
  vmo = TestFixture::MakeStoredVmo(2);
  auto vmo2 = vmo.vmo();
  result = store.Register(std::move(vmo));
  ASSERT_RESULT(result);
  auto k2 = result.take_value();
  ASSERT_NE(k1, k2);
  auto k3 = MakeKey<typename TestFixture::VmoStore::Key>(TestFixture::kStorageCapacity / 2);
  vmo = TestFixture::MakeStoredVmo(3);
  auto vmo3 = vmo.vmo();
  ASSERT_OK(store.RegisterWithKey(k3, std::move(vmo))) << "Failed to register with key " << k3;

  // Can't insert with a used key.
  ASSERT_EQ(store.RegisterWithKey(k1, TestFixture::MakeStoredVmo()), ZX_ERR_ALREADY_EXISTS);

  auto* retrieved = store.GetVmo(k1);
  ASSERT_TRUE(retrieved);
  ASSERT_EQ(retrieved->vmo()->get(), vmo1->get());
  ASSERT_NO_FATAL_FAILURE(CompareMeta(retrieved, 1));

  retrieved = store.GetVmo(k2);
  ASSERT_TRUE(retrieved);
  ASSERT_EQ(retrieved->vmo()->get(), vmo2->get());
  ASSERT_NO_FATAL_FAILURE(CompareMeta(retrieved, 2));

  retrieved = store.GetVmo(k3);
  ASSERT_TRUE(retrieved);
  ASSERT_EQ(retrieved->vmo()->get(), vmo3->get());
  ASSERT_NO_FATAL_FAILURE(CompareMeta(retrieved, 3));

  ASSERT_EQ(store.count(), 3u);

  // Unregister k3 and check that we can't get it anymore nor erase it again.
  ASSERT_OK(store.Unregister(k3));
  ASSERT_EQ(store.Unregister(k3), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(store.GetVmo(k3), nullptr);
  // Check that the VMO handle got destroyed.
  uint64_t vmo_size;
  ASSERT_EQ(vmo3->get_size(&vmo_size), ZX_ERR_BAD_HANDLE);

  ASSERT_EQ(store.count(), 2u);
}

}  // namespace testing
}  // namespace vmo_store
