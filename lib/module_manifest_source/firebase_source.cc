// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/firebase_source.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include <lib/async/cpp/task.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fdio/watcher.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <lib/network_wrapper/network_wrapper_impl.h>

#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/firebase/firebase_impl.h"
#include "peridot/lib/module_manifest_source/xdr.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace modular {

namespace http = ::fuchsia::net::oldhttp;

class FirebaseModuleManifestSource::Watcher : public firebase::WatchClient {
 public:
  Watcher(async_t* async, ModuleManifestSource::IdleFn idle_fn,
          ModuleManifestSource::NewEntryFn new_fn,
          ModuleManifestSource::RemovedEntryFn removed_fn,
          fxl::WeakPtr<FirebaseModuleManifestSource> owner)
      : async_(async),
        idle_fn_(idle_fn),
        new_fn_(new_fn),
        removed_fn_(removed_fn),
        reconnect_wait_seconds_(0),
        owner_(owner) {}
  ~Watcher() override = default;

 private:
  void OnPut(const std::string& path, const rapidjson::Value& value) override {
    // Successful connection established. Reset our reconnect counter.
    reconnect_wait_seconds_ = 0;

    // The first time we're called, we get a dump of all existing values as a
    // JSON object at path "/".
    //
    // From then on, we get notifications of individual values changing with
    // paths like "/module-id".
    if (path == "/") {
      if (value.IsNull()) {
        idle_fn_();
        return;
      }

      if (!value.IsObject()) {
        FXL_LOG(ERROR) << "Got update at /, but it's not a JSON object??";
        idle_fn_();
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
    idle_fn_();

    // Simple exponential backoff counter.
    if (reconnect_wait_seconds_ == 0) {
      reconnect_wait_seconds_ = 1;
    } else {
      reconnect_wait_seconds_ *= 2;
      if (reconnect_wait_seconds_ > 60)
        reconnect_wait_seconds_ = 60;
    }

    // Try to reconnect.
    FXL_LOG(INFO) << "Reconnecting to Firebase in " << reconnect_wait_seconds_
                  << " seconds.";
    async::PostDelayedTask(async_,
                           [owner = owner_, this]() {
                             if (!owner)
                               return;
                             owner->StartWatching(this);
                           },
                           zx::sec(reconnect_wait_seconds_));
  }

  void ProcessEntry(const std::string& name, const rapidjson::Value& value) {
    // We have to deep-copy |it->value| because XdrRead() must have a
    // rapidjson::Document.
    rapidjson::Document doc;
    doc.CopyFrom(value, doc.GetAllocator());

    // Handle bad manifests, including older files expressed as an array.
    // Any mismatch causes XdrRead to DCHECK.
    if (!doc.IsObject()) {
      FXL_LOG(WARNING) << "Ignored invalid manifest: " << name;
      return;
    }

    fuchsia::modular::ModuleManifest entry;
    if (!XdrRead(&doc, &entry, XdrModuleManifest)) {
      FXL_LOG(WARNING) << "Could not parse Module manifest from: " << name;
      return;
    }

    new_fn_(name, std::move(entry));
  }

  async_t* const async_;

  ModuleManifestSource::IdleFn idle_fn_;
  ModuleManifestSource::NewEntryFn new_fn_;
  ModuleManifestSource::RemovedEntryFn removed_fn_;

  int reconnect_wait_seconds_;

  fxl::WeakPtr<FirebaseModuleManifestSource> owner_;
};

FirebaseModuleManifestSource::FirebaseModuleManifestSource(
    async_t* async,
    std::function<http::HttpServicePtr()> network_service_factory,
    std::string db_id, std::string prefix)
    : db_id_(db_id),
      prefix_(prefix),
      network_wrapper_(new network_wrapper::NetworkWrapperImpl(
          async, std::make_unique<backoff::ExponentialBackoff>(),
          std::move(network_service_factory))),
      client_(
          new firebase::FirebaseImpl(network_wrapper_.get(), db_id, prefix)),
      weak_factory_(this) {}

FirebaseModuleManifestSource::~FirebaseModuleManifestSource() {
  for (auto& watcher : watchers_) {
    client_->UnWatch(watcher.get());
  }
}

void FirebaseModuleManifestSource::Watch(async_t* async, IdleFn idle_fn,
                                         NewEntryFn new_fn,
                                         RemovedEntryFn removed_fn) {
  auto watcher = std::make_unique<Watcher>(
      async, std::move(idle_fn), std::move(new_fn), std::move(removed_fn),
      weak_factory_.GetWeakPtr());

  StartWatching(watcher.get());
  watchers_.push_back(std::move(watcher));
}

void FirebaseModuleManifestSource::StartWatching(Watcher* watcher) {
  client_->Watch("/manifests", {}, watcher);
}

}  // namespace modular
