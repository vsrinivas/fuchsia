// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A Module that serves as the recipe in the example story, i.e. that
// creates other Modules in the session.

#include <mojo/system/main.h>
#include <vector>

#include "apps/maxwell/document_store/interfaces/document.mojom.h"
#include "apps/modular/mojo/single_service_application.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/story_runner/story_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/map.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "mojo/public/cpp/system/macros.h"

namespace {

constexpr char kValueLabel[] = "value";
constexpr char kSenderLabel[] = "sender";

using document_store::Document;

using mojo::Binding;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::Map;
using mojo::StrongBinding;
using mojo::String;
using mojo::StructPtr;

using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::ModuleClient;
using modular::ModuleWatcher;
using modular::DocumentEditor;
using modular::Session;

// Implementation of the LinkChanged service that forwards each document
// changed in one Link instance to a second Link instance.
class LinkConnection : public LinkChanged {
 public:
  LinkConnection(InterfacePtr<Link>& src, InterfacePtr<Link>& dst)
      : src_binding_(this), src_(src), dst_(dst) {
    InterfaceHandle<LinkChanged> watcher;
    src_binding_.Bind(GetProxy(&watcher));
    src_->Watch(std::move(watcher));
  }

  void Notify(StructPtr<Document> doc) override {
    FTL_LOG(INFO) << "LinkConnection::Notify() " << DocumentEditor::ToString(doc);
    dst_->AddDocument(std::move(doc));
  }

 private:
  Binding<LinkChanged> src_binding_;
  InterfacePtr<Link>& src_;
  InterfacePtr<Link>& dst_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkConnection);
};

// Implementation of the LinkChanged service that just reports every
// document changed in the given Link.
class LinkMonitor : public LinkChanged {
 public:
  LinkMonitor(const std::string tag, InterfacePtr<Link>& link)
      : binding_(this), tag_(tag) {
    InterfaceHandle<LinkChanged> watcher;
    binding_.Bind(GetProxy(&watcher));
    link->WatchAll(std::move(watcher));
  }

  void Notify(StructPtr<Document> doc) override {
    FTL_LOG(INFO) << "LinkMonitor::Notify()";
  }

 private:
  Binding<LinkChanged> binding_;
  const std::string tag_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(LinkMonitor);
};

class ModuleMonitor : public ModuleWatcher {
 public:
  ModuleMonitor(InterfacePtr<ModuleClient>& module_client,
                InterfacePtr<Session>& session)
      : binding_(this), session_(session) {
    InterfaceHandle<ModuleWatcher> watcher;
    binding_.Bind(GetProxy(&watcher));
    module_client->Watch(std::move(watcher));
  }

  void Done() override { session_->Done(); }

 private:
  Binding<ModuleWatcher> binding_;
  InterfacePtr<Session>& session_;
  MOJO_DISALLOW_COPY_AND_ASSIGN(ModuleMonitor);
};

// Module implementation that acts as a recipe. It implements both
// Module and the LinkChanged observer of its own Link.
class RecipeImpl : public Module, public LinkChanged {
 public:
  explicit RecipeImpl(InterfaceRequest<Module> req)
      : module_binding_(this, std::move(req)), watcher_binding_(this) {
    FTL_LOG(INFO) << "RecipeImpl";
  }
  ~RecipeImpl() override { FTL_LOG(INFO) << "~RecipeImpl"; }

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    FTL_LOG(INFO) << "RecipeImpl::Initialize()";

    // TODO(mesch): Good illustration of the remaining issue to
    // restart a session: How does this code look like when the
    // Session is not new, but already contains existing Modules and
    // Links from the previous execution that is continued here?

    session_.Bind(session.Pass());
    link_.Bind(link.Pass());

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));

    session_->CreateLink(GetProxy(&module1_link_));
    session_->CreateLink(GetProxy(&module2_link_));

    InterfaceHandle<Link> module1_link_handle;  // To pass to StartModule().
    module1_link_->Dup(GetProxy(&module1_link_handle));

    InterfaceHandle<Link> module2_link_handle;  // To pass to StartModule().
    module2_link_->Dup(GetProxy(&module2_link_handle));

    FTL_LOG(INFO) << "recipe start module module1";
    session_->StartModule("mojo:example_module1",
                          std::move(module1_link_handle), GetProxy(&module1_));

    FTL_LOG(INFO) << "recipe start module module2";
    session_->StartModule("mojo:example_module2",
                          std::move(module2_link_handle), GetProxy(&module2_));

    monitors_.emplace_back(new LinkMonitor("module1", module1_link_));
    monitors_.emplace_back(new LinkMonitor("module2", module2_link_));

    connections_.emplace_back(new LinkConnection(module1_link_, module2_link_));
    connections_.emplace_back(new LinkConnection(module2_link_, module1_link_));

    module_monitors_.emplace_back(new ModuleMonitor(module1_, session_));
    module_monitors_.emplace_back(new ModuleMonitor(module2_, session_));

    // This must come last, otherwise we get a notification of our own
    // write because of the "send initial values" code.
    DocumentEditor doc("http://domokit.org/doc/1");
    doc.AddProperty(kValueLabel, DocumentEditor::NewIntValue(1));
    doc.AddProperty(kSenderLabel, DocumentEditor::NewStringValue("RecipeImpl"));
    module1_link_->AddDocument(doc.TakeDocument());
  }

  void Notify(StructPtr<Document> doc) override {
    FTL_LOG(INFO) << "RecipeImpl::Notify()";
  }

 private:
  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Link> link_;
  InterfacePtr<Session> session_;

  InterfacePtr<ModuleClient> module1_;
  InterfacePtr<Link> module1_link_;

  InterfacePtr<ModuleClient> module2_;
  InterfacePtr<Link> module2_link_;

  std::vector<std::unique_ptr<LinkConnection>> connections_;
  std::vector<std::unique_ptr<LinkMonitor>> monitors_;
  std::vector<std::unique_ptr<ModuleMonitor>> module_monitors_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(RecipeImpl);
};

}  // namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "recipe main";
  modular::SingleServiceApplication<Module, RecipeImpl> app;
  return mojo::RunApplication(request, &app);
}
