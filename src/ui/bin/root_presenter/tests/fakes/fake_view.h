// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_VIEW_H_
#define SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_VIEW_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <memory>
#include <optional>

namespace root_presenter::testing {

// The FakeView class provides a means of creating a minimal view
// and listening for scenic events on that view.
class FakeView {
 public:
  FakeView(sys::ComponentContext* component_context, fuchsia::ui::scenic::ScenicPtr scenic);
  ~FakeView() = default;

  bool IsAttachedToScene();

  const std::vector<fuchsia::ui::scenic::Event>& events() const { return events_; }
  void clear_events() { events_.clear(); }

  fuchsia::ui::views::ViewHolderToken view_holder_token() const;

  std::optional<uint32_t> view_id() const;

 private:
  // Connection to scenic.
  fuchsia::ui::scenic::ScenicPtr scenic_;

  // Convenience wrapper for scenic session interface.
  scenic::Session session_;

  // Resources below must be declared after scenic session, because they
  // must be destroyed before the session is destroyed.

  // Holds the a11y view resource.
  // If not present, this view does not exist in the view tree.
  std::optional<scenic::View> fake_view_;

  // View holder token.
  // If null, this view holder token has been std::moved to a new owner.
  std::optional<fuchsia::ui::views::ViewHolderToken> view_holder_token_;

  // Events received for |session_|.
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace root_presenter::testing

#endif  // SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_VIEW_H_
