// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_SOURCE_FIREBASE_SOURCE_H_
#define PERIDOT_LIB_MODULE_MANIFEST_SOURCE_FIREBASE_SOURCE_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "garnet/public/lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/network/fidl/network_service.fidl.h"
#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace firebase {
class Firebase;
}

namespace ledger {
class NetworkService;
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
      fxl::RefPtr<fxl::TaskRunner> task_runner,
      std::function<network::NetworkServicePtr()> network_service_factory,
      std::string db_id,
      std::string prefix);
  ~FirebaseModuleManifestSource() override;

  void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
             IdleFn idle_fn,
             NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override;

 private:
  class Watcher;

  void OnConnectionError();

  // Only used for logging.
  const std::string db_id_;
  const std::string prefix_;

  std::unique_ptr<ledger::NetworkService> network_service_;
  std::unique_ptr<firebase::Firebase> client_;

  std::vector<std::unique_ptr<Watcher>> watchers_;

  fxl::WeakPtrFactory<FirebaseModuleManifestSource> weak_factory_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_SOURCE_FIREBASE_SOURCE_H_