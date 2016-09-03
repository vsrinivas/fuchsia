// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODULAR_STORY_MANAGER_APP_H_
#define MODULAR_STORY_MANAGER_APP_H_

#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"

namespace story_manager {

class StoryManagerApp : public mojo::ApplicationImplBase {
 public:
  StoryManagerApp();
  ~StoryManagerApp() override;

 private:
  // |ApplicationImplBase|:
  void OnInitialize() override;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryManagerApp);
};

}  // namespace story_manager

#endif  // MODULAR_STORY_MANAGER_APP_H_
