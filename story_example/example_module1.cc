// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/mojo/single_service_view_app.h"
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
constexpr char kSenderLabel[] = "sender";

using document_store::Document;
using document_store::Property;
using document_store::Value;

using mojo::ApplicationConnector;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StrongBinding;
using mojo::String;
using mojo::StructPtr;

using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::DocumentEditor;
using modular::Session;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module1Impl : public Module, public LinkChanged {
 public:
  explicit Module1Impl(InterfaceHandle<ApplicationConnector> app_connector,
                       InterfaceRequest<Module> module_request,
                       InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : module_binding_(this, std::move(module_request)),
        watcher_binding_(this) {
    FTL_LOG(INFO) << "Module1Impl";
  }

  ~Module1Impl() override { FTL_LOG(INFO) << "~Module1Impl"; }

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
  void Notify(StructPtr<Document> doc) override {
    FTL_LOG(INFO) << "\nModule1Impl::Notify() " << (int64_t)this
                  << DocumentEditor::ToString(doc);
    DocumentEditor editor(std::move(doc));
    auto v = editor.GetValue(kSenderLabel);
    FTL_DCHECK(v != nullptr);
    v->set_string_value("Module1Impl");

    v = editor.GetValue(kValueLabel);
    FTL_DCHECK(v != nullptr);

    int val = v->get_int_value();
    if (val >= 10) {
      session_->Done();
    } else {
      v->set_int_value(val + 1);
      link_->AddDocument(editor.TakeDocument());
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
  modular::SingleServiceViewApp<modular::Module, Module1Impl> app;
  return mojo::RunApplication(request, &app);
}
