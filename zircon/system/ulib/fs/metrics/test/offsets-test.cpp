// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>
#include <string>
#include <string_view>

#include <fbl/algorithm.h>
#include <fs/metrics/internal/attributes.h>
#include <fs/metrics/internal/object_generator.h>
#include <fs/metrics/internal/offsets.h>
#include <lib/inspect-vmo/inspect.h>
#include <lib/inspect-vmo/types.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {
namespace {

class ObjectGeneratorTest : public zxtest::Test {
public:
    void SetUp() final {
        root_ = inspector_.CreateObject("root-test");
        ASSERT_TRUE(static_cast<bool>(root_));
    }

protected:
    inspect::vmo::Inspector inspector_;
    inspect::vmo::Object root_;
};

struct Data {
    bool attr1 = false;

    uint64_t attr2 = false;

    std::string attr3 = "";
};

// Instead of creating an inspect object, stores the generated name. Allows checking
// that name generation is ok.
void CreateTracker(const char* name, inspect::vmo::Object* root,
                   std::vector<std::string>* name_list) {
    name_list->push_back(name);
}

// Static assert on the properties.
static_assert(BinaryAttribute::kSize == 2, "BinaryAttributes must have size 2.");

// Define fake attributes.
struct Attribute1 : BinaryAttribute {
    static constexpr bool Data::*kAttributeValue = &Data::attr1;

    static std::string ToString(bool value) { return value ? "true" : "false"; }
};

struct Attribute2 : NumericAttribute<Attribute2, uint64_t> {
    static constexpr uint64_t kBuckets[] = {1, 2, 3, 4, 5};

    static constexpr uint64_t Data::*kAttributeValue = &Data::attr2;
};

// Raw attribute that does not conform to Numeric or Binary. Just to cover all cases.
struct Attribute3 {
    static constexpr size_t kSize = 30;
    static constexpr size_t OffsetOf(const std::string_view value) {
        return fbl::min(value.length(), static_cast<size_t>(29));
    }
    static constexpr std::string Data::*kAttributeValue = &Data::attr3;

    static std::string ToString(size_t index) { return fbl::StringPrintf("%zu", index).c_str(); }
};

using TestOffsets = Offsets<Attribute1, Attribute2, Attribute3>;

struct OperationBase {
    using AttributeData = Data;
    static constexpr auto CreateTracker = fs_metrics::CreateTracker;
};

struct Operation1 : OperationBase, Attribute3 {
    static constexpr uint64_t kStart = 0;
    static constexpr char kPrefix[] = "Prefix1";
};

struct Operation2 : OperationBase, Attribute1, Attribute2 {
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
    data.attr1 = false;
    data.attr2 = 5;
    data.attr3 = "hello!";

    ASSERT_EQ(TestOffsets::RelativeOffset<Operation1>(data), 6);
    ASSERT_EQ(TestOffsets::RelativeOffset<Operation2>(data), 10);
}

TEST(OffsetsTest, AbsoluteOffsetCalculatedBasedAttributes) {
    Data data;
    data.attr1 = false;
    data.attr2 = 5;
    data.attr3 = "hello!";

    ASSERT_EQ(TestOffsets::AbsoluteOffset<Operation1>(data), 6 + TestOffsets::Begin<Operation1>());
    ASSERT_EQ(TestOffsets::AbsoluteOffset<Operation2>(data), 10 + TestOffsets::Begin<Operation2>());
}

using TestObjectGenerator = ObjectGenerator<Attribute1, Attribute2, Attribute3>;

TEST_F(ObjectGeneratorTest, GeneratedObjectsMatchObjectCount) {
    std::vector<std::string> generated_objects;

    TestObjectGenerator::AddObjects<Operation1>(&root_, &generated_objects);
    EXPECT_EQ(generated_objects.size(), TestOffsets::Count<Operation1>());

    generated_objects.clear();

    TestObjectGenerator::AddObjects<Operation2>(&root_, &generated_objects);
    EXPECT_EQ(generated_objects.size(), TestOffsets::Count<Operation2>());
}

TEST_F(ObjectGeneratorTest, GeneratedObjectsNameMatchRule) {
    std::vector<std::string> generated_objects;

    TestObjectGenerator::AddObjects<Operation2>(&root_, &generated_objects);
    ASSERT_EQ(generated_objects.size(), TestOffsets::Count<Operation2>());

    // The output is based on the order the attributes are inherited from in the operation.
    std::set<std::string> expected_set = {
        "Prefix2_false_-inf_1", "Prefix2_false_1_2",   "Prefix2_false_2_3",   "Prefix2_false_3_4",
        "Prefix2_false_4_5",    "Prefix2_false_5_inf", "Prefix2_true_-inf_1", "Prefix2_true_1_2",
        "Prefix2_true_2_3",     "Prefix2_true_3_4",    "Prefix2_true_4_5",    "Prefix2_true_5_inf",
    };

    ASSERT_EQ(generated_objects.size(), expected_set.size());
    for (const auto& name : generated_objects) {
        EXPECT_NE(expected_set.find(name), expected_set.end(), "%s is missing in the generated set",
                  name.c_str());
    }
}

} // namespace
} // namespace fs_metrics
