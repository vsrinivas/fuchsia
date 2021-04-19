// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/vmo_store/owned_vmo_store.h"

#include <ostream>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

namespace vmo_store {
namespace testing {

class OwnershipTest : public ::testing::Test {
 public:
  static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE;
  using Store = ::vmo_store::OwnedVmoStore<HashTableStorage<size_t>>;
  using Agent = typename Store::RegistrationAgent;
  OwnershipTest() : ::testing::Test(), store_(Options()) {}

  zx::vmo CreateVmo() {
    zx::vmo vmo;
    EXPECT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
    return vmo;
  }

  Store store_;
};

TEST_F(OwnershipTest, FailedRegistrationDoesNotIncreaseCount) {
  Agent agent(&store_);
  auto result = agent.Register(zx::vmo());
  ASSERT_TRUE(result.is_error()) << "Result has unexpected key: " << result.value();
  ASSERT_STATUS(result.status_value(), ZX_ERR_BAD_HANDLE);

  ASSERT_EQ(store_.count(), 0u);
}

TEST_F(OwnershipTest, TwoAgentsRegisterWithKey) {
  constexpr size_t kKey1 = 1;
  constexpr size_t kKey2 = 2;
  auto agent1 = store_.CreateRegistrationAgent();
  auto agent2 = store_.CreateRegistrationAgent();

  zx::vmo vmo = CreateVmo();
  const zx_handle_t vmo1 = vmo.get();
  ASSERT_OK(agent1.RegisterWithKey(kKey1, std::move(vmo)));

  // Agent2 uses the same namespace, can't register again with key1.
  ASSERT_STATUS(agent2.RegisterWithKey(kKey1, CreateVmo()), ZX_ERR_ALREADY_EXISTS);

  vmo = CreateVmo();
  const zx_handle_t vmo2 = vmo.get();
  ASSERT_OK(agent2.RegisterWithKey(kKey2, std::move(vmo)));

  ASSERT_EQ(store_.count(), 2u);

  // Each agent can get their own VMOs.
  ASSERT_NE(agent1.GetVmo(kKey1), nullptr);
  ASSERT_NE(agent2.GetVmo(kKey2), nullptr);
  // Each agent can't get each other's VMOs.
  ASSERT_EQ(agent1.GetVmo(kKey2), nullptr);
  ASSERT_EQ(agent2.GetVmo(kKey1), nullptr);
  // Each agent can't unregister each other's VMOs.
  {
    auto status = agent1.Unregister(kKey2);
    ASSERT_TRUE(status.is_error());
    ASSERT_STATUS(status.status_value(), ZX_ERR_ACCESS_DENIED);
  }
  {
    auto status = agent2.Unregister(kKey1);
    ASSERT_TRUE(status.is_error());
    ASSERT_STATUS(status.status_value(), ZX_ERR_ACCESS_DENIED);
  }

  {
    auto status = agent1.Unregister(kKey1);
    ASSERT_TRUE(status.is_ok()) << zx_status_get_string(status.status_value());
    ASSERT_EQ(status.value().get(), vmo1);
  }
  {
    auto status = agent2.Unregister(kKey2);
    ASSERT_TRUE(status.is_ok()) << zx_status_get_string(status.status_value());
    ASSERT_EQ(status.value().get(), vmo2);
  }

  ASSERT_EQ(store_.count(), 0u);
}

TEST_F(OwnershipTest, TwoAgentsRegister) {
  // Test that RegistrationAgent::Register also works with two agents.
  auto agent1 = store_.CreateRegistrationAgent();
  auto agent2 = store_.CreateRegistrationAgent();
  auto result1 = agent1.Register(CreateVmo());
  ASSERT_TRUE(result1.is_ok()) << "Failed to register VMO: "
                               << zx_status_get_string(result1.status_value());
  auto result2 = agent2.Register(CreateVmo());
  ASSERT_TRUE(result2.is_ok()) << "Failed to register VMO: "
                               << zx_status_get_string(result2.status_value());

  ASSERT_EQ(store_.count(), 2u);

  auto k1 = result1.value();
  auto k2 = result2.value();
  ASSERT_NE(k1, k2);
  // Each agent can get their own VMOs.
  ASSERT_NE(agent1.GetVmo(k1), nullptr);
  ASSERT_NE(agent2.GetVmo(k2), nullptr);
  // Each agent can't get each other's VMOs.
  ASSERT_EQ(agent1.GetVmo(k2), nullptr);
  ASSERT_EQ(agent2.GetVmo(k1), nullptr);

  // Each agent can't unregister each other's VMOs.
  {
    auto status = agent1.Unregister(k2);
    ASSERT_TRUE(status.is_error());
    ASSERT_STATUS(status.status_value(), ZX_ERR_ACCESS_DENIED);
  }

  {
    auto status = agent2.Unregister(k1);
    ASSERT_TRUE(status.is_error());
    ASSERT_STATUS(status.status_value(), ZX_ERR_ACCESS_DENIED);
  }

  {
    auto status = agent1.Unregister(k1);
    ASSERT_TRUE(status.is_ok()) << zx_status_get_string(status.status_value());
  }

  {
    auto status = agent2.Unregister(k2);
    ASSERT_TRUE(status.is_ok()) << zx_status_get_string(status.status_value());
  }

  ASSERT_EQ(store_.count(), 0u);
}

}  // namespace testing
}  // namespace vmo_store
