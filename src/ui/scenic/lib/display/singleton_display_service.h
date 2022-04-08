// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_SINGLETON_DISPLAY_SERVICE_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_SINGLETON_DISPLAY_SERVICE_H_

#include <fuchsia/ui/display/singleton/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/outgoing_directory.h>

#include <memory>

#include "src/ui/scenic/lib/display/display.h"

namespace scenic_impl::display {

// Implements the fuchsia.ui.display.singleton.Info FIDL service.
class SingletonDisplayService : public fuchsia::ui::display::singleton::Info {
 public:
  explicit SingletonDisplayService(std::shared_ptr<display::Display> display);

  // |fuchsia::ui::display::singleton::Info|
  void GetMetrics(fuchsia::ui::display::singleton::Info::GetMetricsCallback callback) override;

  // Registers this service impl in |outgoing_directory|.  This service impl object must then live
  // for as long as it is possible for any service requests to be made.
  void AddPublicService(sys::OutgoingDirectory* outgoing_directory);

 private:
  const std::shared_ptr<display::Display> display_ = nullptr;
  fidl::BindingSet<fuchsia::ui::display::singleton::Info> bindings_;
};

}  // namespace scenic_impl::display

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_SINGLETON_DISPLAY_SERVICE_H_
