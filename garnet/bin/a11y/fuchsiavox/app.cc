// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/fuchsiavox/app.h"

namespace fuchsiavox {

App::App()
    : startup_context_(sys::ComponentContext::Create()),
      fuchsiavox_(std::make_unique<FuchsiavoxImpl>(startup_context_.get())),
      gesture_detector_(std::make_unique<GestureDetector>(fuchsiavox_.get())) {}
}  // namespace fuchsiavox
