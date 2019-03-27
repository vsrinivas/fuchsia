// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_FAKES_FAKE_VIEW_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_FAKES_FAKE_VIEW_H_

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "lib/fidl/cpp/binding.h"

namespace media_player {
namespace test {

// Implements View for testing.
class FakeView : public ::fuchsia::ui::viewsv1::View,
                 public ::fuchsia::sys::ServiceProvider {
 public:
  FakeView();

  ~FakeView() override;

  const ::fuchsia::ui::viewsv1::ViewListenerPtr& view_listener() {
    return view_listener_;
  }

  // Binds the view.
  void Bind(fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
            zx::eventpair view_token,
            ::fuchsia::ui::viewsv1::ViewListenerPtr listener,
            zx::eventpair parent_export_token, fidl::StringPtr label);

  void GetServiceProvider(
      fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> service_provider)
      override;

  void OfferServiceProvider(
      fidl::InterfaceHandle<::fuchsia::sys::ServiceProvider> service_provider,
      std::vector<std::string> service_names) override;

  void GetContainer(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewContainer> container)
      override;

  // ServiceProvider implementation.
  void ConnectToService(std::string name, zx::channel channel) override;

 private:
  async_dispatcher_t* dispatcher_;
  fidl::Binding<::fuchsia::ui::viewsv1::View> binding_;
  ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener_;
  zx::eventpair view_token_;
  zx::eventpair parent_export_token_;
  std::string label_;

  // ServiceProvider fields.
  fidl::Binding<::fuchsia::sys::ServiceProvider> service_provider_binding_;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_FAKES_FAKE_VIEW_H_
