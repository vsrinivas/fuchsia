// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect_deprecated/reader.h>
#include <lib/inspect_deprecated/testing/inspect.h>
#include <zircon/types.h>

using inspect::Inspector;
using testing::UnorderedElementsAre;
using namespace inspect_deprecated::testing;

namespace {

TEST(VmoReader, CreateAndReadObjectHierarchy) {
  auto inspector = std::make_unique<inspect::Inspector>("objects");
  ASSERT_TRUE(inspector);

  auto& object = inspector->GetRoot();
  auto req = object.CreateChild("requests");
  auto network = req.CreateUint("network", 10);
  auto wifi = req.CreateUint("wifi", 5);
  auto volume = object.CreateDouble("volume", 0.75);
  auto assets = object.CreateInt("assets", -100);

  auto version = object.CreateString("version", "1.0beta2");

  char dump[4000];
  memset(dump, 'a', 5);
  memset(dump + 5, 'b', 4000 - 5);
  auto dump_prop = req.CreateByteVector("dump", std::vector<uint8_t>());
  dump_prop.Set(std::vector<uint8_t>(dump, dump + 4000));

  inspect::Snapshot snapshot;
  ASSERT_EQ(ZX_OK, inspect::Snapshot::Create(*inspector->GetVmo().value(), &snapshot));

  std::vector<fit::result<inspect_deprecated::ObjectHierarchy>> hierarchies;
  hierarchies.emplace_back(inspect_deprecated::ReadFromSnapshot(std::move(snapshot)));
  hierarchies.emplace_back(inspect_deprecated::ReadFromVmo(*inspector->GetVmo().value()));
  for (auto& root : hierarchies) {
    ASSERT_TRUE(root.is_ok());
    EXPECT_THAT(
        root.value(),
        AllOf(NodeMatches(
                  AllOf(NameMatches("objects"),
                        PropertyList(UnorderedElementsAre(StringPropertyIs("version", "1.0beta2"))),
                        MetricList(UnorderedElementsAre(DoubleMetricIs("volume", 0.75),
                                                        IntMetricIs("assets", -100))))),
              ChildrenMatch(UnorderedElementsAre(NodeMatches(
                  AllOf(NameMatches("requests"),
                        PropertyList(UnorderedElementsAre(ByteVectorPropertyIs(
                            "dump", std::vector((uint8_t*)dump, (uint8_t*)dump + 4000)))),
                        MetricList(UnorderedElementsAre(UIntMetricIs("network", 10),
                                                        UIntMetricIs("wifi", 5)))))))));
  }
}

}  // namespace
