// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_TEST_FAKES_FAKE_VIEW_MANAGER_H_
#define GARNET_BIN_MEDIAPLAYER_TEST_FAKES_FAKE_VIEW_MANAGER_H_

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "garnet/bin/mediaplayer/test/fakes/fake_view.h"
#include "lib/fidl/cpp/binding.h"

namespace media_player {
namespace test {

class FakeScenic;

// Implements ViewManager for testing.
class FakeViewManager : public ::fuchsia::ui::viewsv1::ViewManager {
 public:
  FakeViewManager(FakeScenic* fake_scenic);

  ~FakeViewManager() override;

  // Binds the renderer.
  void Bind(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewManager> request);

  // ViewManager implementation.
  void GetScenic(
      fidl::InterfaceRequest<::fuchsia::ui::scenic::Scenic> request) override;

  void CreateView(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner> view_owner,
      fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewListener> view_listener,
      zx::eventpair parent_export_token, fidl::StringPtr label) override;

  void CreateViewTree(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewTree> view_tree,
      fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewTreeListener>
          view_tree_listener,
      fidl::StringPtr label) override;

 private:
  async_dispatcher_t* dispatcher_;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewManager> binding_;
  FakeScenic* fake_scenic_;
  FakeView fake_view_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_TEST_FAKES_FAKE_VIEW_MANAGER_H_
