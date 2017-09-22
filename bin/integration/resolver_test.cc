// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/resolver/resolver.fidl.h"

#include "apps/maxwell/src/integration/test.h"

namespace {

class ResolverTest : public MaxwellTestBase {
 public:
  ResolverTest()
      : resolver_(ConnectToService<resolver::Resolver>("resolver")) {}

 protected:
  resolver::ResolverPtr resolver_;
};

}  // namespace

TEST_F(ResolverTest, ResolveToModule) {
  fidl::Array<resolver::ModuleInfoPtr> modules;
  resolver_->ResolveModules(
      "https://fuchsia-contracts.google.com/hello_contract", nullptr,
      [&](fidl::Array<resolver::ModuleInfoPtr> modules_) {
        modules = std::move(modules_);
      });
  ASYNC_EQ(1, modules.size());
  EXPECT_EQ("https://www.example.com/hello", modules[0]->component_id);
}

// Ensure that invalid JSON does not result in a call that never completes.
TEST_F(ResolverTest, ResolveWithInvalidData) {
  bool completed = false;
  resolver_->ResolveModules(
      "foo contract", "not valid JSON",
      [&](fidl::Array<resolver::ModuleInfoPtr> modules_) { completed = true; });
  ASYNC_CHECK(completed);
}
