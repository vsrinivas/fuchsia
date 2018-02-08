// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_MOZART_APP_H_
#define GARNET_BIN_UI_MOZART_APP_H_

#include <memory>

#include "garnet/lib/ui/mozart/clock.h"
#include "garnet/lib/ui/mozart/mozart.h"

namespace mz {

class App {
 public:
  explicit App(app::ApplicationContext* app_context);

 private:
  Clock clock_;
  std::unique_ptr<Mozart> mozart_;
  fidl::BindingSet<Mozart> bindings_;
};

}  // namespace mz

#endif  // GARNET_BIN_UI_MOZART_APP_H_
