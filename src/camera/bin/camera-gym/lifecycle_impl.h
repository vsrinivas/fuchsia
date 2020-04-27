// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_LIFECYCLE_IMPL_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_LIFECYCLE_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

// This class implements the Lifecycle protocol by invoking a caller-provided closure on Terminate.
class LifecycleImpl : public fuchsia::modular::Lifecycle {
 public:
  explicit LifecycleImpl(fit::closure on_terminate);
  ~LifecycleImpl() override;
  fidl::InterfaceRequestHandler<fuchsia::modular::Lifecycle> GetHandler();

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::modular::Lifecycle> request);

  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

  async::Loop loop_;
  fit::closure on_terminate_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> bindings_;
};

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_LIFECYCLE_IMPL_H_
