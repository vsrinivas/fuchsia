// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/talkback/app.h"

namespace talkback {

App::App()
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      talkback_(std::make_unique<TalkbackImpl>(startup_context_.get())),
      gesture_detector_(std::make_unique<GestureDetector>(
          startup_context_.get(), talkback_.get())) {}

}  // namespace talkback
