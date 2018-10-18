// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_COMPONENT_H_
#define LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_COMPONENT_H_

#include <lib/async-loop/cpp/loop.h>
#include <memory>

#include "lib/component/cpp/startup_context.h"
#include "lib/ui/base_view/cpp/view_provider_service.h"

namespace scenic {

// Provides a skeleton for an entire component that only offers a ViewProvider
// service. This is only intended to be used for simple example programs.
class ViewProviderComponent {
 public:
  ViewProviderComponent(ViewFactory factory, async::Loop* loop,
                        component::StartupContext* startup_context = nullptr);
  ViewProviderComponent(V1ViewFactory factory, async::Loop* loop,
                        component::StartupContext* startup_context = nullptr);
  ViewProviderComponent(const ViewProviderComponent&) = delete;
  ViewProviderComponent& operator=(const ViewProviderComponent&) = delete;
  ~ViewProviderComponent() = default;

 private:
  std::unique_ptr<component::StartupContext> startup_context_;
  fidl::InterfacePtr<fuchsia::ui::scenic::Scenic> scenic_;
  ViewProviderService service_;
};

}  // namespace scenic

#endif  // LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_COMPONENT_H_
