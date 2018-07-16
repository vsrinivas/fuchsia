// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_LIFECYCLE_CPP_LIFECYCLE_IMPL_H_
#define LIB_LIFECYCLE_CPP_LIFECYCLE_IMPL_H_

#include <functional>
#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/macros.h>
#include <lib/svc/cpp/service_namespace.h>

namespace modular {

class LifecycleImpl : fuchsia::modular::Lifecycle {
 public:
  // Users of LifecycleImpl register a delegate to receive Terminate().
  class Delegate {
   public:
    virtual void Terminate() = 0;
  };

  // |Delegate.Terminate()| is called when a
  // fuchsia::modular::Lifecycle.Terminate message is received.
  LifecycleImpl(component::ServiceNamespace* service_namespace,
                Delegate* delegate);

 private:
  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

  Delegate* const delegate_;
  fidl::Binding<fuchsia::modular::Lifecycle> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LifecycleImpl);
};

}  // namespace modular

#endif  // LIB_LIFECYCLE_CPP_LIFECYCLE_IMPL_H_
