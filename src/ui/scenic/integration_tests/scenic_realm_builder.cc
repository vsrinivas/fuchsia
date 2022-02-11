// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/integration_tests/scenic_realm_builder.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>

namespace {
// Name for the scenic subrealm.
constexpr auto kScenicSubRealm = "scenic_subrealm";
}  // namespace

namespace integration_tests {
using Route = sys::testing::Route;
using RealmRoot = sys::testing::experimental::RealmRoot;
using Protocol = sys::testing::Protocol;
using ChildRef = sys::testing::ChildRef;
using ParentRef = sys::testing::ParentRef;
using RealmBuilder = sys::testing::experimental::RealmBuilder;

ScenicRealmBuilder::ScenicRealmBuilder(const SubRealmUrl& url)
    : realm_builder_(RealmBuilder::Create()) {
  Init(url);
}

ScenicRealmBuilder& ScenicRealmBuilder::Init(const SubRealmUrl& url) {
  realm_builder_.AddChild(kScenicSubRealm, url);

  // Route the protocols required by the scenic subrealm from the test_manager.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::vulkan::loader::Loader::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kScenicSubRealm}}});

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddScenicSubRealmProtocol(const ProtocolName& protocol) {
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{protocol}},
                                .source = ChildRef{kScenicSubRealm},
                                .targets = {ParentRef()}});

  return *this;
}

std::unique_ptr<RealmRoot> ScenicRealmBuilder::Build() {
  return std::make_unique<RealmRoot>(realm_builder_.Build());
}
}  // namespace integration_tests
