// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/element/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <zxtest/zxtest.h>

namespace {

constexpr auto kReferenceElementV2Url = "#meta/reference-element.cm";

class ElementManagerTest : public zxtest::Test {};

// Tests that proposing an element returns a successful result.
TEST_F(ElementManagerTest, ProposeElement) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  fuchsia::element::ManagerPtr manager;
  auto context = sys::ComponentContext::Create();
  EXPECT_EQ(ZX_OK, context->svc()->Connect(manager.NewRequest()));

  manager.set_error_handler([&](zx_status_t status) {
    EXPECT_EQ(ZX_OK, status);
    loop.Quit();
  });

  fuchsia::element::Spec spec;
  spec.set_component_url(kReferenceElementV2Url);

  bool is_proposed{false};
  manager->ProposeElement(std::move(spec), /*controller=*/nullptr,
                          [&](fuchsia::element::Manager_ProposeElement_Result result) {
                            EXPECT_FALSE(result.is_err());
                            is_proposed = true;
                            loop.Quit();
                          });

  loop.Run();

  EXPECT_TRUE(is_proposed);
}

}  // namespace
