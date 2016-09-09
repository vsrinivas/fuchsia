// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>
#include <stdio.h>

#include "apps/modular/mojom_hack/story_runner.mojom.h"
#include "apps/modular/story-example/module_app.h"
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

using story::Link;
using story::LinkChanged;
using story::Module;
using story::Session;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module2Impl : public Module, public LinkChanged {
 public:
  explicit Module2Impl(InterfaceRequest<Module> req)
      : module_binding_(this, std::move(req)), watcher_binding_(this) {}
  ~Module2Impl() override {}

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    FTL_LOG(INFO) << "module2 init";

    session_.Bind(session.Pass());
    link_.Bind(link.Pass());

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // Whenever the module sees a changed "in" value, it increments it
  // by 1 and writes it to "out".
  //
  // TODO(mesch): This is one possible workaround to avoid change
  // callbacks for the module's own changes. This needs to be solved
  // better, because the Schema in the Link is supposed to represent
  // data structure, not data flow, and hence having in and out fields
  // in the schema is an abuse.
  void Value(const mojo::String& label, const mojo::String& value) override {
    if (label == "in" && value != "") {
      FTL_LOG(INFO) << "module2 value \"" << value << "\"";

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

  MOJO_DISALLOW_COPY_AND_ASSIGN(Module2Impl);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "module2 main";
  story::ModuleApp<Module2Impl> app;
  return mojo::RunApplication(request, &app);
}
