// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

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

namespace {

// Subjects
constexpr char kDocId[] =
    "http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5";

// Property labels
constexpr char kCounterLabel[] = "http://schema.domokit.org/counter";
constexpr char kSenderLabel[] = "http://schema.org/sender";

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Property;
using document_store::Value;

using mojo::ApplicationConnector;
using mojo::Array;
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
using modular::operator<<;

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

    session_.Bind(std::move(session));
    link_.Bind(std::move(link));

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // See comments on Module2Impl in example-module2.cc.
  void Notify(Array<DocumentPtr> docs) override {
    FTL_LOG(INFO) << "Module1Impl::Notify() " << (int64_t)this << docs;

    DocumentEditor editor;
    if (!editor.TakeFromArray(kDocId, &docs)) return;

    Value* sender = editor.GetValue(kSenderLabel);
    Value* value = editor.GetValue(kCounterLabel);
    FTL_DCHECK(value != nullptr);

    int counter = value->get_int_value();
    if (counter > 10) {
      // For the last iteration, Module2 removes the sender.
      FTL_DCHECK(sender == nullptr);
      session_->Done();
    } else {
      FTL_DCHECK(sender != nullptr);
      value->set_int_value(counter + 1);
      sender->set_string_value("Module1Impl");

      docs.push_back(editor.TakeDocument());
      link_->SetAllDocuments(std::move(docs));
    }
  }

 private:
  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Session> session_;
  InterfacePtr<Link> link_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Module1Impl);
};
}
// namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "module1 main";
  modular::SingleServiceViewApp<modular::Module, Module1Impl> app;
  return mojo::RunApplication(request, &app);
}
