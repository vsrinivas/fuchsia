// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <strstream>

#include "apps/modular/mojo/single_service_application.h"
#include "apps/modular/story_runner/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/environment/logging.h"
#include "mojo/public/cpp/system/macros.h"

namespace {

using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StrongBinding;
using mojo::String;

using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::Session;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module1Impl : public Module, public LinkChanged {
 public:
  explicit Module1Impl(InterfaceRequest<Module> req)
      : module_binding_(this, std::move(req)), watcher_binding_(this) {}
  ~Module1Impl() override {}

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    FTL_LOG(INFO) << "module1 init";

    session_.Bind(session.Pass());
    link_.Bind(link.Pass());

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // See comments on Module2Impl in example-module2.cc.
  void Value(const String& label, const String& value) override {
    if (label == "in" && value != "") {
      FTL_LOG(INFO) << "module1 value \"" << value << "\"";

      int i = 0;
      std::istringstream(value.get()) >> i;
      ++i;
      std::ostringstream out;
      out << i;
      link_->SetValue("in", "");
      link_->SetValue("out", out.str());
    }
  }

 private:
  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Session> session_;
  InterfacePtr<Link> link_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(Module1Impl);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "module1 main";
  modular::SingleServiceApplication<Module, Module1Impl> app;
  return mojo::RunApplication(request, &app);
}
