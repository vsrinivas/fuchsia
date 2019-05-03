// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENIC_APP_H_
#define GARNET_BIN_UI_SCENIC_APP_H_

#include <lib/fit/function.h>

#include <memory>

#include "garnet/lib/ui/scenic/scenic.h"

namespace scenic_impl {

class App {
 public:
  explicit App(sys::ComponentContext* app_context, inspect::Node inspect_node,
               fit::closure quit_callback);

 private:
  std::unique_ptr<Scenic> scenic_;
  fidl::BindingSet<Scenic> bindings_;
};

}  // namespace scenic_impl

#endif  // GARNET_BIN_UI_SCENIC_APP_H_
