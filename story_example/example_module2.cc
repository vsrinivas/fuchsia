// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <stdio.h>

#include "apps/modular/mojo/single_service_application.h"
#include "apps/modular/story_runner/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "mojo/public/cpp/environment/logging.h"
#include "mojo/public/cpp/system/macros.h"

namespace {

constexpr char kValueLabel[] = "value";

using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StrongBinding;
using mojo::String;
using mojo::StructPtr;

using modular::Link;
using modular::LinkChanged;
using modular::LinkValue;
using modular::Module;
using modular::Session;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module2Impl : public Module, public LinkChanged {
 public:
  explicit Module2Impl(InterfaceRequest<Module> req)
      : module_binding_(this, std::move(req)), watcher_binding_(this) {
    FTL_LOG(INFO) << "Module2Impl";
  }

  ~Module2Impl() override {
    FTL_LOG(INFO) << "~Module2Impl";
  }

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    FTL_LOG(INFO) << "module2 init";

    session_.Bind(session.Pass());
    link_.Bind(link.Pass());

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // Whenever the module sees a changed value, it increments it by 1
  // and writes it back. This works because the module is not notified
  // of changes from itself. More precisely, a watcher registered
  // through one link handle is not notified of changes requested
  // through the same handle. It's really the handle identity that
  // decides.
  void Value(StructPtr<LinkValue> value) override {
    StructPtr<LinkValue>& v = value->get_object_value()[kValueLabel];
    v->set_int_value(v->get_int_value() + 1);
    link_->SetValue(std::move(value));
  }

 private:
  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Session> session_;
  InterfacePtr<Link> link_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(Module2Impl);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "module2 main";
  modular::SingleServiceApplication<Module, Module2Impl> app;
  return mojo::RunApplication(request, &app);
}
