// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/app.h"

namespace a11y_manager {

App::App()
    : startup_context_(component::StartupContext::CreateFromStartupInfo()) {
  FXL_DCHECK(startup_context_);
  FXL_LOG(INFO) << "Publishing a11y manager service";
  a11y_manager_.reset(new ManagerImpl());
}

App::~App() {}

}  // namespace a11y_manager
