// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/metrics/offsets.h"

#include <lib/inspect/cpp/inspect.h>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/metrics/attributes.h"
#include "src/lib/storage/vfs/cpp/metrics/object_generator.h"

namespace fs_metrics {
namespace {

class NodeGeneratorTest : public zxtest::Test {
 public:
  NodeGeneratorTest() : inspector_() {}

 protected:
  inspect::Inspector inspector_;
};

struct Data {
  uint64_t attr1 = false;

  bool attr2 = false;

  std::string attr3 = "";
};

// Instead of creating an inspect object, stores the generated name. Allows checking that name
// generation is ok.
void CreateTracker(const char* name, inspect::Node* root, std::vector<std::string>* name_list) {
  name_list->push_back(name);
}

// Static assert on the properties.
static_assert(BinaryAttribute::kSize == 2, "BinaryAttributes must have size 2.");

// Define fake attributes.

struct Attribute1 : NumericAttribute<Attribute1, uint64_t> {
  static constexpr uint64_t kBuckets[] = {1, 2, 3, 4, 5};

  static constexpr uint64_t Data::*kAttributeValue = &Data::attr1;
};

struct Attribute2 : BinaryAttribute {
  static constexpr bool Data::*kAttributeValue = &Data::attr2;

  static std::string ToString(bool value) { return value ? "true" : "false"; }
};

// Raw attribute that does not conform to Numeric or Binary. Just to cover all cases.
struct Attribute3 {
  static constexpr size_t kSize = 30;
  static constexpr size_t OffsetOf(const std::string_view value) {
    return std::min(value.length(), static_cast<size_t>(29));
  }
  static constexpr std::string Data::*kAttributeValue = &Data::attr3;

  static std::string ToString(size_t index) { return fbl::StringPrintf("%zu", index).c_str(); }
};

// Order here matters, and needs to match the order of inheritance in any objects using
// these attributes, since AddObjects will visit each attribute in the order defined below.
// For example, if struct A: Attribute2, Attribute1 and struct B: Attribute3, the order of the
// attributes passed to the ObjectGenerator template must be either 2,1,3 or 3,2,1.
// If instead struct B: Attribute3, Attribute2, the order *must* be 3,2,1.
using TestOffsets = Offsets<Attribute1, Attribute2, Attribute3>;

struct OperationBase {
  using AttributeData = Data;
  static constexpr auto CreateTracker = fs_metrics::CreateTracker;
};

struct Operation1 : OperationBase, Attribute3 {
  static constexpr uint64_t kStart = 0;
  static constexpr char kPrefix[] = "Prefix1";
};

struct Operation2 : OperationBase, Attribute2, Attribute1 {
  static constexpr uint64_t kStart = TestOffsets::End<Operation1>();
  static constexpr char kPrefix[] = "Prefix2";
};

TEST(OffsetsTest, CountIsProductOfAttributeSizes) {
  ASSERT_EQ(TestOffsets::Count<Operation1>(), Attribute3::kSize);
  ASSERT_EQ(TestOffsets::Count<Operation2>(), Attribute2::kSize * Attribute1::kSize);
}

TEST(OffsetsTest, EndMatchesCountPlusBegin) {
  ASSERT_EQ(TestOffsets::End<Operation1>(),
            TestOffsets::Begin<Operation1>() + TestOffsets::Count<Operation1>());
  ASSERT_EQ(TestOffsets::End<Operation2>(),
            TestOffsets::Begin<Operation2>() + TestOffsets::Count<Operation2>());
}

TEST(OffsetsTest, RelativeOffsetCalculatedBasedAttributes) {
  Data data;
  data.attr1 = 5;
  data.attr2 = false;
  data.attr3 = "hello!";

  EXPECT_EQ(TestOffsets::RelativeOffset<Operation1>(data), 6);
  EXPECT_EQ(TestOffsets::RelativeOffset<Operation2>(data), 5);

  data.attr2 = true;
  EXPECT_EQ(TestOffsets::RelativeOffset<Operation2>(data), 11);

  data.attr1 = 4;
  EXPECT_EQ(TestOffsets::RelativeOffset<Operation2>(data), 10);
}

TEST(OffsetsTest, AbsoluteOffsetCalculatedBasedAttributes) {
  Data data;
  data.attr1 = 5;
  data.attr2 = false;
  data.attr3 = "hello!";

  EXPECT_EQ(TestOffsets::AbsoluteOffset<Operation1>(data), 6 + TestOffsets::Begin<Operation1>());
  EXPECT_EQ(TestOffsets::AbsoluteOffset<Operation2>(data), 5 + TestOffsets::Begin<Operation2>());

  data.attr2 = true;
  EXPECT_EQ(TestOffsets::AbsoluteOffset<Operation2>(data), 11 + TestOffsets::Begin<Operation2>());

  data.attr1 = 4;
  EXPECT_EQ(TestOffsets::AbsoluteOffset<Operation2>(data), 10 + TestOffsets::Begin<Operation2>());
}

using TestNodeGenerator = ObjectGenerator<Attribute1, Attribute2, Attribute3>;

TEST_F(NodeGeneratorTest, GeneratedNodesMatchNodeCount) {
  std::vector<std::string> generated_objects;

  TestNodeGenerator::AddObjects<Operation1>(&inspector_.GetRoot(), &generated_objects);
  EXPECT_EQ(generated_objects.size(), TestOffsets::Count<Operation1>());

  generated_objects.clear();

  TestNodeGenerator::AddObjects<Operation2>(&inspector_.GetRoot(), &generated_objects);
  EXPECT_EQ(generated_objects.size(), TestOffsets::Count<Operation2>());
}

TEST_F(NodeGeneratorTest, GeneratedNodesNameMatchRule) {
  std::vector<std::string> generated_objects;

  TestNodeGenerator::AddObjects<Operation2>(&inspector_.GetRoot(), &generated_objects);
  ASSERT_EQ(generated_objects.size(), TestOffsets::Count<Operation2>());

  // The output is based on the order the attributes are inherited from in the operation and is
  // visited last-to-first based on their order in ObjectGenerator's variadic template parameter.
  std::vector<std::string> expected_objects = {
      "Prefix2_false_-inf_1", "Prefix2_false_1_2",   "Prefix2_false_2_3",   "Prefix2_false_3_4",
      "Prefix2_false_4_5",    "Prefix2_false_5_inf", "Prefix2_true_-inf_1", "Prefix2_true_1_2",
      "Prefix2_true_2_3",     "Prefix2_true_3_4",    "Prefix2_true_4_5",    "Prefix2_true_5_inf",
  };

  ASSERT_EQ(generated_objects.size(), expected_objects.size());
  // Order of the generated objects must match exactly that we expect.
  for (size_t i = 0; i < expected_objects.size(); ++i) {
    EXPECT_EQ(generated_objects[i], expected_objects[i]);
  }
}

}  // namespace
}  // namespace fs_metrics
