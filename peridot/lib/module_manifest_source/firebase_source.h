// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_SOURCE_FIREBASE_SOURCE_H_
#define PERIDOT_LIB_MODULE_MANIFEST_SOURCE_FIREBASE_SOURCE_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace firebase {
class Firebase;
}

namespace network_wrapper {
class NetworkWrapper;
}

namespace modular {

// Watches a Manifest repository stored in Firebase.
// Expects the database to be constructed like so:
// {
//   "manifests": {
//     "module id 1": [ manifest entry, manifest entry, ... ],
//     "module id 2": ...
//   }
// }
class FirebaseModuleManifestSource : public ModuleManifestSource {
 public:
  FirebaseModuleManifestSource(
      async_dispatcher_t* dispatcher,
      std::function<::fuchsia::net::oldhttp::HttpServicePtr()>
          network_service_factory,
      std::string db_id, std::string prefix);
  ~FirebaseModuleManifestSource() override;

  void Watch(async_dispatcher_t* dispatcher, IdleFn idle_fn, NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override;

 private:
  class Watcher;

  void StartWatching(Watcher* watcher);

  // Only used for logging.
  const std::string db_id_;
  const std::string prefix_;

  std::unique_ptr<network_wrapper::NetworkWrapper> network_wrapper_;
  std::unique_ptr<firebase::Firebase> client_;

  std::vector<std::unique_ptr<Watcher>> watchers_;

  fxl::WeakPtrFactory<FirebaseModuleManifestSource> weak_factory_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_SOURCE_FIREBASE_SOURCE_H_
