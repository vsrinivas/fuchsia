// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/node_add_args.h>

#include <gtest/gtest.h>

namespace {

namespace fcd = fuchsia_component_decl;

constexpr const char kServiceName[] = "Service";
constexpr const char kInstanceName[] = "Instance";

void CheckOffer(fcd::Offer offer) {
  ASSERT_EQ(offer.Which(), fcd::Offer::Tag::kService);
  ASSERT_TRUE(offer.service()->source_name().has_value());
  ASSERT_EQ(offer.service()->source_name().value(), kServiceName);
  ASSERT_TRUE(offer.service()->target_name().has_value());
  ASSERT_EQ(offer.service()->source_name().value(), kServiceName);

  ASSERT_TRUE(offer.service()->renamed_instances().has_value());
  auto& mapping = offer.service()->renamed_instances().value();
  ASSERT_EQ(mapping.size(), 1ul);
  ASSERT_EQ(mapping[0].source_name(), kInstanceName);
  ASSERT_EQ(mapping[0].target_name(), "default");

  ASSERT_TRUE(offer.service()->source_instance_filter().has_value());
  auto& filter = offer.service()->source_instance_filter().value();
  ASSERT_EQ(filter.size(), 1ul);
  ASSERT_EQ(filter[0], "default");
}

TEST(NodeAddArgsTest, MakeOfferNatural) {
  auto offer = driver::MakeOffer(kServiceName, kInstanceName);
  CheckOffer(offer);
}

TEST(NodeAddArgsTest, MakeOfferWire) {
  fidl::Arena arena;
  auto offer = fidl::ToNatural(driver::MakeOffer(arena, kServiceName, kInstanceName));
  CheckOffer(offer);
}

}  // namespace
