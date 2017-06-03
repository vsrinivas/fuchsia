// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "application/services/application_environment.fidl.h"
#include "apps/mozart/services/scene/composer.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"

namespace mozart {
namespace composer {

class ComposerImpl;

class ComposerApp {
 public:
  class Params {
   public:
    bool Setup(const ftl::CommandLine& command_line) { return true; }
  };

  explicit ComposerApp(Params* params);
  ~ComposerApp();

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;

  fidl::BindingSet<mozart2::Composer, std::unique_ptr<ComposerImpl>>
      composer_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ComposerApp);
};

}  // namespace composer
}  // namespace mozart
