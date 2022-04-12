// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_EMBEDDER_VIEW_H_
#define SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_EMBEDDER_VIEW_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <zircon/status.h>

namespace embedder_tests {

struct ViewContext {
  scenic::SessionPtrAndListenerRequest session_and_listener_request;
  fuchsia::ui::views::ViewToken view_token;
};

class EmbedderView : public fuchsia::ui::scenic::SessionListener {
 public:
  EmbedderView(ViewContext context, fuchsia::ui::views::ViewHolderToken view_holder_token)
      : binding_(this, std::move(context.session_and_listener_request.second)),
        session_(std::move(context.session_and_listener_request.first)),
        view_(&session_, std::move(context.view_token), "View"),
        top_node_(&session_),
        view_holder_(&session_, std::move(view_holder_token), "ViewHolder") {
    binding_.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "Session listener binding: " << zx_status_get_string(status);
    });
    view_.AddChild(top_node_);
    // Call |Session::Present| in order to flush events having to do with
    // creation of |view_| and |top_node_|.
    session_.Present(0, [](auto) {});
  }

  void EmbedView(std::function<void(fuchsia::ui::gfx::ViewState)> view_state_changed_callback) {
    view_state_changed_callback_ = std::move(view_state_changed_callback);
    top_node_.Attach(view_holder_);
    session_.Present(0, [](auto) {});
  }

 private:
  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    for (const auto& event : events) {
      if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
          event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
        const auto& evt = event.gfx().view_properties_changed();

        view_holder_.SetViewProperties(evt.properties);
        session_.Present(0, [](auto) {});

      } else if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
                 event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewStateChanged) {
        const auto& evt = event.gfx().view_state_changed();
        if (evt.view_holder_id == view_holder_.id()) {
          // Clients of |EmbedderView| *must* set a view state changed
          // callback.  Failure to do so is a usage error.
          FX_CHECK(view_state_changed_callback_);
          view_state_changed_callback_(evt.state);
        }
      }
    }
  }

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicError(std::string error) override { FX_LOGS(FATAL) << "OnScenicError: " << error; }

  fidl::Binding<fuchsia::ui::scenic::SessionListener> binding_;
  scenic::Session session_;
  scenic::View view_;
  scenic::EntityNode top_node_;
  std::optional<fuchsia::ui::gfx::ViewProperties> embedded_view_properties_;
  scenic::ViewHolder view_holder_;
  std::function<void(fuchsia::ui::gfx::ViewState)> view_state_changed_callback_;
};

}  // namespace embedder_tests

#endif  // SRC_UI_TESTS_INTEGRATION_FLUTTER_TESTS_EMBEDDER_EMBEDDER_VIEW_H_
