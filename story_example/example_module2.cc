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

// Subjects
constexpr char kDocId[] =
    "http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5";

// Property labels
constexpr char kCounterLabel[] = "http://schema.domokit.org/counter";
constexpr char kSenderLabel[] = "http://schema.org/sender";

using document_store::Document;
using document_store::DocumentPtr;
using document_store::Value;

using mojo::ApplicationConnector;
using mojo::Array;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::StrongBinding;
using mojo::String;

using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::DocumentEditor;
using modular::Session;
using modular::operator<<;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module2Impl : public Module, public LinkChanged {
 public:
  explicit Module2Impl(InterfaceHandle<ApplicationConnector> app_connector,
                       InterfaceRequest<Module> module_request,
                       InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : module_binding_(this, std::move(module_request)),
        watcher_binding_(this) {
    FTL_LOG(INFO) << "Module2Impl";
  }

  ~Module2Impl() override { FTL_LOG(INFO) << "~Module2Impl"; }

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
  void Notify(mojo::Array<document_store::DocumentPtr> docs) override {
    FTL_LOG(INFO) << "Module2Impl::Notify() " << (int64_t)this << docs;

    DocumentEditor editor;
    if (!editor.TakeFromArray(kDocId, &docs)) return;

    Value* sender = editor.GetValue(kSenderLabel);
    Value* counter = editor.GetValue(kCounterLabel);

    FTL_DCHECK(sender != nullptr);
    FTL_DCHECK(counter != nullptr);

    sender->set_string_value("Module2Impl");

    int n = counter->get_int_value() + 1;
    counter->set_int_value(n);

    // For the last value, remove the sender property to prove that property
    // removal works.
    if (n == 11) {
      editor.RemoveProperty(kSenderLabel);
    }

    Array<DocumentPtr> array;
    array.push_back(editor.TakeDocument());
    link_->SetAllDocuments(std::move(array));
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
  modular::SingleServiceViewApp<Module, Module2Impl> app;
  return mojo::RunApplication(request, &app);
}
