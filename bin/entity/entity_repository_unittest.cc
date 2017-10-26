// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/entity/entity_repository.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/testing/mock_base.h"
#include "peridot/lib/testing/test_with_message_loop.h"

// TODO(vardhan): A unittest with a module/agent passing an entity between
// each other using an entity reference.

namespace modular {
namespace testing {
namespace {

bool UnorderedCompare(std::vector<std::string> a, std::vector<std::string> b) {
  return std::set<std::string>(a.begin(), a.end()) ==
         std::set<std::string>(b.begin(), b.end());
}

class EntityRepositoryTest : public TestWithMessageLoop, public MockBase {
 public:
  EntityRepositoryTest() = default;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(EntityRepositoryTest);
};

void ExpectEntityEq(Entity* entity,
                    std::vector<std::string> types,
                    std::vector<std::vector<uint8_t>> contents,
                    size_t* pending_expects) {
  // Test that we read the correct types.
  *pending_expects = 1;
  entity->GetTypes([pending_expects,
                    types](fidl::Array<fidl::String> result) mutable {
    --*pending_expects;
    EXPECT_TRUE(UnorderedCompare(result.To<std::vector<std::string>>(), types));
  });

  // Test that we read the correct content for each type.
  (*pending_expects) += types.size();
  for (size_t i = 0; i < types.size(); i++) {
    entity->GetContent(
        types[i], [expected_content = contents[i], pending_expects,
                   &contents](fidl::Array<uint8_t> result) mutable {
          --*pending_expects;
          EXPECT_EQ(expected_content, result.To<std::vector<uint8_t>>());
        });
  }
}

// Tests:
//  - Creating an entity
//  - Expecting correct types and contents in the new entity
//  - Checking a reference dereferences to the same entity again.
TEST_F(EntityRepositoryTest, BasicStoreListRetrieve) {
  EntityRepository repository;
  EntityStorePtr store;
  EntityResolverPtr resolver;
  repository.ConnectEntityStore(store.NewRequest());
  repository.ConnectEntityResolver(resolver.NewRequest());

  // Make an entity
  std::vector<std::string> types = {"type1", "type2"};
  fidl::Array<fidl::Array<uint8_t>> data;
  data.push_back(to_array("data1"));
  data.push_back(to_array("data2"));
  EntityPtr entity;
  store->CreateEntity(fidl::Array<fidl::String>::From(types), data.Clone(),
                      entity.NewRequest());

  // 1. Test that entity doesn't close.
  entity.set_connection_error_handler(
      [] { EXPECT_TRUE(false) << "Could not create entity."; });

  size_t pending_expects = 0;
  ExpectEntityEq(entity.get(), types,
                 data.To<std::vector<std::vector<uint8_t>>>(),
                 &pending_expects);
  ASSERT_GT(pending_expects, 0ul);
  RunLoopUntil([&pending_expects] { return pending_expects == 0; });
  EXPECT_TRUE(pending_expects == 0);

  // 3. Test that we can get a reference from the entity.
  EntityReferencePtr reference;
  entity->GetReference(
      [&reference](EntityReferencePtr ref) { reference = std::move(ref); });

  // Blocks for 3.
  RunLoopUntil([&reference] { return !reference.is_null(); });
  EXPECT_TRUE(!reference.is_null());

  // Entity was created at this point, so clear connection handler.
  entity.set_connection_error_handler(nullptr);

  // 4. Test that we get the same entity back when we dereference.
  EntityPtr entity2;
  resolver->GetEntity(reference.Clone(), entity2.NewRequest());

  // 4.1. Test that entity2 doesn't close.
  entity2.set_connection_error_handler(
      [] { EXPECT_TRUE(false) << "Could not dereference entity2."; });

  // 4.2. Test that entity2 == entity1.
  ExpectEntityEq(entity2.get(), types,
                 data.To<std::vector<std::vector<uint8_t>>>(),
                 &pending_expects);
  ASSERT_GT(pending_expects, 0ul);
  RunLoopUntil([&pending_expects] { return pending_expects == 0; });
  EXPECT_TRUE(pending_expects == 0);
}

}  // namespace
}  // namespace testing
}  // namespace modular
