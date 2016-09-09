// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Module that serves as the recipe in the example story, i.e. that
// creates other Modules in the session.

#include <mojo/system/main.h>
#include <stdio.h>
#include <vector>

#include "apps/modular/mojom_hack/story_runner.mojom.h"
#include "apps/modular/story-example/module_app.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/system/macros.h"

namespace {

using mojo::Binding;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StrongBinding;

using story::Link;
using story::LinkChanged;
using story::Module;
using story::Session;

// Implementation of the LinkChanged service that forwards each value
// changed in one Link instance to a second Link instance.
class LinkConnection : public LinkChanged {
 public:
  LinkConnection(InterfacePtr<Link>& src, InterfacePtr<Link>& dst)
      : src_binding_(this), src_(src), dst_(dst) {
    InterfaceHandle<LinkChanged> watcher;
    src_binding_.Bind(GetProxy(&watcher));
    src_->Watch(std::move(watcher));
  }

  void Value(const mojo::String& label, const mojo::String& value) override {
    if (label == "out" && value != "") {
      FTL_LOG(INFO) << "recipe link connection value \"" << value << "\"";
      src_->SetValue("out", "");
      dst_->SetValue("in", value);
    }
  }

 private:
  Binding<LinkChanged> src_binding_;
  InterfacePtr<Link>& src_;
  InterfacePtr<Link>& dst_;
};

// Module implementation that acts as a recipe. It implements both
// Module and the LinkChanged observer of its own Link.
class RecipeImpl : public Module, public LinkChanged {
 public:
  explicit RecipeImpl(InterfaceRequest<Module> req)
      : module_binding_(this, std::move(req)), watcher_binding_(this) {}
  ~RecipeImpl() override {}

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    FTL_LOG(INFO) << "recipe init";

    // TODO(mesch): Good illustration of the remaining issue to
    // restart a session: How does this code look like when the
    // Session is not new, but already contains existing Modules and
    // Links from the previous execution that is continued here?

    session_.Bind(session.Pass());
    link_.Bind(link.Pass());

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));

    session_->CreateLink("token_pass", GetProxy(&module1_link_));

    InterfaceHandle<Link> module1_link_handle;  // To pass to StartModule().
    module1_link_->Dup(GetProxy(&module1_link_handle));

    FTL_LOG(INFO) << "recipe start module module1";
    session_->StartModule("mojo:example-module1",
                          std::move(module1_link_handle),
                          [this](InterfaceHandle<Module> module) {
                            FTL_LOG(INFO) << "recipe start module module1 done";
                            module1_.Bind(std::move(module));
                            module1_link_->SetValue("in", "1");
                          });

    session_->CreateLink("token_pass", GetProxy(&module2_link_));

    InterfaceHandle<Link> module2_link_handle;  // To pass to StartModule().
    module2_link_->Dup(GetProxy(&module2_link_handle));

    FTL_LOG(INFO) << "recipe start module module2";
    session_->StartModule("mojo:example-module2",
                          std::move(module2_link_handle),
                          [this](InterfaceHandle<Module> module) {
                            FTL_LOG(INFO) << "recipe start module module2 done";
                            module2_.Bind(std::move(module));
                          });

    connections_.emplace_back(new LinkConnection(module1_link_, module2_link_));
    connections_.emplace_back(new LinkConnection(module2_link_, module1_link_));
  }

  void Value(const mojo::String& label, const mojo::String& value) override {
    FTL_LOG(INFO) << "recipe value \"" << label << "\", \"" << value << "\"";
  }

 private:
  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Link> link_;
  InterfacePtr<Session> session_;

  InterfacePtr<Module> module1_;
  InterfacePtr<Link> module1_link_;

  InterfacePtr<Module> module2_;
  InterfacePtr<Link> module2_link_;

  std::vector<std::shared_ptr<LinkConnection>> connections_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(RecipeImpl);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "recipe main";
  story::ModuleApp<RecipeImpl> app;
  return mojo::RunApplication(request, &app);
}
