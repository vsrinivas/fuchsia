// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/firebase_source.h"

#include <errno.h>
#include <fcntl.h>
#include <fdio/watcher.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/bin/cloud_provider_firebase/firebase/firebase_impl.h"
#include "peridot/bin/cloud_provider_firebase/network/network_service_impl.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace {

// TODO(thatguy): This is duplicated from directory_source. Put into a shared
// file.
void XdrNounConstraint(
    modular::XdrContext* const xdr,
    ModuleManifestSource::Entry::NounConstraint* const data) {
  xdr->Field("name", &data->name);
  xdr->Field("types", &data->types);
}

void XdrEntry(modular::XdrContext* const xdr,
              ModuleManifestSource::Entry* const data) {
  xdr->Field("binary", &data->binary);
  xdr->Field("local_name", &data->local_name);
  xdr->Field("verb", &data->verb);
  xdr->Field("noun_constraints", &data->noun_constraints, XdrNounConstraint);
}

}  // namespace

class FirebaseModuleManifestSource::Watcher : public firebase::WatchClient {
 public:
  Watcher(ModuleManifestSource::IdleFn idle_fn,
          ModuleManifestSource::NewEntryFn new_fn,
          ModuleManifestSource::RemovedEntryFn removed_fn,
          fxl::WeakPtr<FirebaseModuleManifestSource> owner)
      : idle_fn_(idle_fn),
        new_fn_(new_fn),
        removed_fn_(removed_fn),
        owner_(owner) {}
  ~Watcher() override = default;

 private:
  void OnPut(const std::string& path, const rapidjson::Value& value) override {
    // The first time we're called, we get a dump of all existing values as a
    // JSON object at path "/".
    //
    // From then on, we get notifications of individual values changing with
    // paths like "/module-id".
    if (path == "/") {
      if (!value.IsObject()) {
        FXL_LOG(ERROR) << "Got update at /, but it's not a JSON object??";
        return;
      }

      for (rapidjson::Value::ConstMemberIterator it = value.MemberBegin();
           it != value.MemberEnd(); ++it) {
        // When we get updates, the path is "/<module id>", so we add "/" here
        // to the member name.
        ProcessEntry(path + it->name.GetString(), it->value);
      }

      // We've read all existing entries.
      idle_fn_();
    } else {
      if (value.IsNull()) {
        removed_fn_(path);
      } else {
        ProcessEntry(path, value);
      }
    }
  }
  void OnPatch(const std::string& path,
               const rapidjson::Value& value) override {}
  void OnCancel() override {}
  void OnAuthRevoked(const std::string& reason) override {}

  void OnMalformedEvent() override {}

  void OnConnectionError() override {
    if (owner_)
      owner_->OnConnectionError();
  }

  void ProcessEntry(const std::string& name, const rapidjson::Value& value) {
    // We have to deep-copy |it->value| becaues XdrRead() must have a
    // rapidjson::Document.
    rapidjson::Document doc;
    doc.CopyFrom(value, doc.GetAllocator());
    Entry entry;
    if (!modular::XdrRead(&doc, &entry, XdrEntry)) {
      FXL_LOG(WARNING) << "Could not parse Module manifest from: " << name;
      return;
    }

    new_fn_(name, entry);
  }

  ModuleManifestSource::IdleFn idle_fn_;
  ModuleManifestSource::NewEntryFn new_fn_;
  ModuleManifestSource::RemovedEntryFn removed_fn_;

  fxl::WeakPtr<FirebaseModuleManifestSource> owner_;
};

FirebaseModuleManifestSource::FirebaseModuleManifestSource(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    std::function<network::NetworkServicePtr()> network_service_factory,
    std::string db_id,
    std::string prefix)
    : db_id_(db_id),
      prefix_(prefix),
      network_service_(
          new ledger::NetworkServiceImpl(std::move(task_runner),
                                         std::move(network_service_factory))),
      client_(
          new firebase::FirebaseImpl(network_service_.get(), db_id, prefix)),
      weak_factory_(this) {}

FirebaseModuleManifestSource::~FirebaseModuleManifestSource() {
  for (auto& watcher : watchers_) {
    client_->UnWatch(watcher.get());
  }
}

void FirebaseModuleManifestSource::Watch(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    IdleFn idle_fn,
    NewEntryFn new_fn,
    RemovedEntryFn removed_fn) {
  auto watcher = std::make_unique<Watcher>(
      std::move(idle_fn), std::move(new_fn), std::move(removed_fn),
      weak_factory_.GetWeakPtr());
  client_->Watch("/manifests", {}, watcher.get());

  watchers_.push_back(std::move(watcher));
}

void FirebaseModuleManifestSource::OnConnectionError() {
  FXL_LOG(ERROR) << db_id_ << " " << prefix_
                 << ": Connection error. No more updates will be processed.";
}

}  // namespace modular
