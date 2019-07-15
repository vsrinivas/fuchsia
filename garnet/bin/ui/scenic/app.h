// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENIC_APP_H_
#define GARNET_BIN_UI_SCENIC_APP_H_

#include <lib/fit/function.h>
#include <lib/fsl/io/device_watcher.h>

#include <memory>

#include "garnet/lib/ui/scenic/scenic.h"
#include "src/ui/lib/escher/escher.h"

namespace scenic_impl {

class App {
 public:
  explicit App(sys::ComponentContext* app_context, inspect_deprecated::Node inspect_node,
               fit::closure quit_callback);

 private:
  escher::EscherUniquePtr escher_;
  std::unique_ptr<Scenic> scenic_;
  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;
};

}  // namespace scenic_impl

#endif  // GARNET_BIN_UI_SCENIC_APP_H_
