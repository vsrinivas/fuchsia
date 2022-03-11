// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/integration_tests/scenic_realm_builder.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>

namespace integration_tests {
namespace {

// Name for the scenic subrealm.
constexpr auto kScenicRealm = "scenic_realm";
constexpr auto kScenicRealmUrl = "#meta/scenic_realm.cm";

}  // namespace

using Route = sys::testing::Route;
using RealmRoot = sys::testing::experimental::RealmRoot;
using Protocol = sys::testing::Protocol;
using ChildRef = sys::testing::ChildRef;
using ParentRef = sys::testing::ParentRef;
using RealmBuilder = sys::testing::experimental::RealmBuilder;

ScenicRealmBuilder::ScenicRealmBuilder() : realm_builder_(RealmBuilder::Create()) { Init(); }

ScenicRealmBuilder& ScenicRealmBuilder::Init() {
  realm_builder_.AddChild(kScenicRealm, kScenicRealmUrl);

  // Route the protocols required by the scenic subrealm from the test_manager.
  realm_builder_.AddRoute(
      Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_},
                             Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                             Protocol{fuchsia::sysmem::Allocator::Name_},
                             Protocol{fuchsia::tracing::provider::Registry::Name_},
                             Protocol{fuchsia::vulkan::loader::Loader::Name_}},
            .source = ParentRef(),
            .targets = {ChildRef{kScenicRealm}}});

  return *this;
}

ScenicRealmBuilder& ScenicRealmBuilder::AddRealmProtocol(const ProtocolName& protocol) {
  realm_builder_.AddRoute(Route{.capabilities = {Protocol{protocol}},
                                .source = ChildRef{kScenicRealm},
                                .targets = {ParentRef()}});

  return *this;
}

RealmRoot ScenicRealmBuilder::Build() { return realm_builder_.Build(); }

}  // namespace integration_tests
