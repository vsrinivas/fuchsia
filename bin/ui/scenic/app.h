// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENIC_APP_H_
#define GARNET_BIN_UI_SCENIC_APP_H_

#include <memory>

#include "garnet/lib/ui/scenic/clock.h"
#include "garnet/lib/ui/scenic/scenic.h"

namespace scenic {

class App {
 public:
  explicit App(component::ApplicationContext* app_context);

 private:
  Clock clock_;
  std::unique_ptr<Scenic> scenic_;
  fidl::BindingSet<Scenic> bindings_;
};

}  // namespace scenic

#endif  // GARNET_BIN_UI_SCENIC_APP_H_
