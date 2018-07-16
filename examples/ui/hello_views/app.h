// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_HELLO_VIEWS_APP_H_
#define GARNET_EXAMPLES_UI_HELLO_VIEWS_APP_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

namespace hello_views {

enum class AppType : unsigned char { CONTAINER, SUBVIEW };

class App : public fuchsia::sys::ServiceProvider {
 public:
  App(async::Loop* loop, AppType type);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  // |sys::ServiceProvider|
  void ConnectToService(fidl::StringPtr service_name, ::zx::channel channel) {}

 private:
  // Called asynchronously when the session dies.
  void ReleaseSessionResources();

  // Updates and presents the scene.  Called first by Init().  Each invocation
  // schedules another call to Update() when the result of the previous
  // presentation is asynchronously received.
  void Update(uint64_t next_presentation_time);

  // Creates all of the scene resources and sets up the scene graph.
  void CreateScene(float display_width, float display_height);

  std::unique_ptr<component::StartupContext> startup_context_;
  async::Loop* const loop_;

  AppType type_;
  fidl::BindingSet<fuchsia::sys::ServiceProvider> service_bindings_;
  fuchsia::sys::ServiceProviderPtr incoming_services_;
  fuchsia::ui::app::ViewProviderPtr view_provider_;
  std::unique_ptr<fuchsia::ui::app::ViewProvider> view_provider_impl_;

  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::DisplayCompositor> compositor_;
  std::unique_ptr<scenic::Camera> camera_;
  uint32_t view_id_ = 0;
  uint32_t root_node_id_ = 0;
};

}  // namespace hello_views

#endif  // GARNET_EXAMPLES_UI_HELLO_VIEWS_APP_H_
