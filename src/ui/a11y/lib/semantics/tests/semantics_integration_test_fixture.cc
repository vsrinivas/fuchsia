// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <zircon/status.h>

#include <vector>

#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {

namespace {

constexpr zx::duration kTimeout = zx::sec(60);

}  // namespace

SemanticsIntegrationTest::SemanticsIntegrationTest(const std::string& environment_label)
    : environment_label_(environment_label),
      component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      view_manager_(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                    std::make_unique<MockViewSemanticsFactory>(),
                    std::make_unique<MockAnnotationViewFactory>(), component_context_.get(),
                    component_context_->outgoing()->debug_dir()),
      scenic_(component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>()) {
  scenic_.set_error_handler([](zx_status_t status) {
    FAIL() << "Lost connection to Scenic: " << zx_status_get_string(status);
  });

  // Wait for scenic to get initialized by calling GetDisplayInfo.
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo info) { QuitLoop(); });
  RunLoopWithTimeout(kTimeout);
}

void SemanticsIntegrationTest::SetUp() {
  TestWithEnvironment::SetUp();
  // This is done in |SetUp| as opposed to the constructor to allow subclasses the opportunity to
  // override |CreateServices()|.
  auto services = TestWithEnvironment::CreateServices();
  services->AddService(semantics_manager_bindings_.GetHandler(&view_manager_));

  CreateServices(services);

  environment_ = CreateNewEnclosingEnvironment(environment_label_, std::move(services),
                                               {.inherit_parent_services = true});
}

fuchsia::ui::views::ViewToken SemanticsIntegrationTest::CreatePresentationViewToken() {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto presenter = real_services()->Connect<fuchsia::ui::policy::Presenter>();
  presenter.set_error_handler(
      [](zx_status_t status) { FAIL() << "presenter: " << zx_status_get_string(status); });
  presenter->PresentView(std::move(view_holder_token), nullptr);

  return std::move(view_token);
}

const Node* SemanticsIntegrationTest::FindNodeWithLabel(const Node* node, zx_koid_t view_ref_koid,
                                                        std::string label) {
  if (!node) {
    return nullptr;
  }

  if (node->has_attributes() && node->attributes().has_label() &&
      node->attributes().label() == label) {
    return node;
  }

  if (!node->has_child_ids()) {
    return nullptr;
  }
  for (const auto& child_id : node->child_ids()) {
    const auto* child = view_manager()->GetSemanticNode(view_ref_koid, child_id);
    FX_DCHECK(child);
    auto result = FindNodeWithLabel(child, view_ref_koid, label);
    if (result != nullptr) {
      return result;
    }
  }

  return nullptr;
}

}  // namespace accessibility_test
