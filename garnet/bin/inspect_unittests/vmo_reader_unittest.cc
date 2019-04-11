// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/reader.h>

#include <gtest/gtest.h>
#include <lib/inspect-vmo/inspect.h>
#include <lib/inspect-vmo/snapshot.h>
#include <lib/inspect-vmo/state.h>
#include <lib/inspect/testing/inspect.h>
#include <zircon/types.h>

using inspect::vmo::DoubleMetric;
using inspect::vmo::Inspector;
using inspect::vmo::IntMetric;
using inspect::vmo::Object;
using inspect::vmo::Property;
using inspect::vmo::UintMetric;
using testing::UnorderedElementsAre;
using namespace inspect::testing;

namespace {

TEST(VmoReader, CreateAndReadObjectHierarchy) {
  auto inspector = std::make_unique<inspect::vmo::Inspector>();
  ASSERT_TRUE(inspector);

  Object object = inspector->CreateObject("objects");
  auto req = object.CreateChild("requests");
  auto network = req.CreateUintMetric("network", 10);
  auto wifi = req.CreateUintMetric("wifi", 5);
  auto volume = object.CreateDoubleMetric("volume", 0.75);
  auto assets = object.CreateIntMetric("assets", -100);

  auto version = object.CreateProperty("version", "1.0beta2",
                                       inspect::vmo::PropertyFormat::kUtf8);

  char dump[4001];
  memset(dump, 'a', 5);
  memset(dump + 5, 'b', 4000 - 5);
  auto dump_prop =
      req.CreateProperty("dump", "", inspect::vmo::PropertyFormat::kBinary);
  dump_prop.Set({dump, 4000});
  dump[4000] = '\0';

  inspect::vmo::Snapshot snapshot;
  ASSERT_EQ(ZX_OK,
            inspect::vmo::Snapshot::Create(inspector->GetVmo(), &snapshot));

  std::vector<fit::result<inspect::ObjectHierarchy>> hierarchies;
  hierarchies.emplace_back(inspect::ReadFromSnapshot(std::move(snapshot)));
  hierarchies.emplace_back(inspect::ReadFromVmo(inspector->GetVmo()));
  for (auto& root : hierarchies) {
    ASSERT_TRUE(root.is_ok());
    EXPECT_THAT(
        root.value(),
        AllOf(
            ObjectMatches(AllOf(
                NameMatches("objects"),
                PropertyList(UnorderedElementsAre(
                    StringPropertyIs("version", "1.0beta2"))),
                MetricList(UnorderedElementsAre(DoubleMetricIs("volume", 0.75),
                                                IntMetricIs("assets", -100))))),
            ChildrenMatch(UnorderedElementsAre(ObjectMatches(AllOf(
                NameMatches("requests"),
                PropertyList(UnorderedElementsAre(ByteVectorPropertyIs(
                    "dump",
                    std::vector((uint8_t*)dump, (uint8_t*)dump + 4000)))),
                MetricList(UnorderedElementsAre(UIntMetricIs("network", 10),
                                                UIntMetricIs("wifi", 5)))))))));
  }
}

}  // namespace
