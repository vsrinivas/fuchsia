// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_VIEW_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_VIEW_H_

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "lib/fidl/cpp/binding.h"

namespace media_player {
namespace test {

// Implements View for testing.
class FakeView : public ::fuchsia::ui::views_v1::View,
                 public ::fuchsia::sys::ServiceProvider,
                 public ::fuchsia::ui::input::InputConnection {
 public:
  FakeView();

  ~FakeView() override;

  const ::fuchsia::ui::views_v1::ViewListenerPtr& view_listener() {
    return view_listener_;
  }

  // Binds the view.
  void Bind(fidl::InterfaceRequest<::fuchsia::ui::views_v1::View> view_request,
            fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
                view_owner_request,
            ::fuchsia::ui::views_v1::ViewListenerPtr listener,
            zx::eventpair parent_export_token, fidl::StringPtr label);

  // View implementation.
  void GetToken(GetTokenCallback callback) override;

  void GetServiceProvider(
      fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> service_provider)
      override;

  void OfferServiceProvider(
      fidl::InterfaceHandle<::fuchsia::sys::ServiceProvider> service_provider,
      fidl::VectorPtr<fidl::StringPtr> service_names) override;

  void GetContainer(
      fidl::InterfaceRequest<::fuchsia::ui::views_v1::ViewContainer> container)
      override;

  // ServiceProvider implementation.
  void ConnectToService(fidl::StringPtr name, zx::channel channel) override;

  // InputConnection implementation.
  void SetEventListener(
      fidl::InterfaceHandle<::fuchsia::ui::input::InputListener> listener)
      override;

  void GetInputMethodEditor(
      ::fuchsia::ui::input::KeyboardType keyboard_type,
      ::fuchsia::ui::input::InputMethodAction action,
      ::fuchsia::ui::input::TextInputState initial_state,
      fidl::InterfaceHandle<::fuchsia::ui::input::InputMethodEditorClient>
          client,
      fidl::InterfaceRequest<::fuchsia::ui::input::InputMethodEditor> editor)
      override;

 private:
  class Owner : public ::fuchsia::ui::views_v1_token::ViewOwner {
   public:
    Owner();

    ~Owner() override;

    // Binds the view owner.
    void Bind(fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
                  view_owner_request);

    // ViewOwner implementation.
    void GetToken(GetTokenCallback callback) override;

   private:
    fidl::Binding<::fuchsia::ui::views_v1_token::ViewOwner> binding_;
  };

  async_dispatcher_t* dispatcher_;
  fidl::Binding<::fuchsia::ui::views_v1::View> binding_;
  ::fuchsia::ui::views_v1::ViewListenerPtr view_listener_;
  zx::eventpair parent_export_token_;
  std::string label_;
  Owner owner_;

  // ServiceProvider fields.
  fidl::Binding<::fuchsia::sys::ServiceProvider> service_provider_binding_;

  // InputConnection fields.
  fidl::Binding<::fuchsia::ui::input::InputConnection>
      input_connection_binding_;
  ::fuchsia::ui::input::InputListenerPtr input_view_listener_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_TEST_FAKES_FAKE_VIEW_H_
