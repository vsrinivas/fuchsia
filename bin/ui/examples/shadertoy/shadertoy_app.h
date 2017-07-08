// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "application/lib/app/application_context.h"
#include "apps/mozart/examples/shadertoy/shadertoy_factory_impl.h"
#include "escher/escher.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

#include "apps/mozart/examples/shadertoy/compiler.h"
#include "apps/mozart/examples/shadertoy/renderer.h"

// A thin wrapper that manages connections to a ShadertoyFactoryImpl singleton.
// TODO: clean up when there are no remaining bindings to Shadertoy nor
// ShadertoyFactory.  What is the best-practice pattern to use here?
class ShadertoyApp {
 public:
  ShadertoyApp(app::ApplicationContext* app_context, escher::Escher* escher);
  ~ShadertoyApp();

  escher::Escher* escher() const { return escher_; }
  Compiler* compiler() { return &compiler_; }
  Renderer* renderer() { return &renderer_; }

 private:
  std::unique_ptr<ShadertoyFactoryImpl> factory_;
  fidl::BindingSet<ShadertoyFactory> bindings_;
  escher::Escher* const escher_;
  Compiler compiler_;
  Renderer renderer_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ShadertoyApp);
};
