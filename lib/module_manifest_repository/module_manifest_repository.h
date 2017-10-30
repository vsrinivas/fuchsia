// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_
#define PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_

#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace modular {

// Watches a directory for new Module manifest files, parses them, and notifies
// each watcher of each entry within.
class ModuleManifestRepository {
 public:
  struct Entry;
  using NewEntryFn = std::function<void(Entry)>;

  // Watches |repository_dir| for new Module manifest files, parses them and
  // notifies |fn| of each entry in the new files. Requires a MessageLoop
  // already exists on the current thread. Calls to |fn| will be posted
  // to the MessageLoop of the thread that created this instance.
  //
  // |repository_dir| must already exist in the process's namespace before
  // calling this constructor.
  //
  // TODO(thatguy): This class does not currently notify anyone when an entry
  // is removed.
  ModuleManifestRepository(std::string repository_dir, NewEntryFn fn);
  ~ModuleManifestRepository();

 private:
  void OnNewFile(const std::string& name);

  const std::string repository_dir_;
  NewEntryFn new_entry_fn_;

  fxl::WeakPtrFactory<ModuleManifestRepository> weak_factory_;
};

// These fields mirror those in the Module manifest doc:
// https://fuchsia.googlesource.com/peridot/+/HEAD/docs/modular/manifests/module.md
struct ModuleManifestRepository::Entry {
  struct NounConstraint;

  std::string binary;
  std::string local_name;
  std::string verb;
  std::vector<NounConstraint> noun_constraints;
};

struct ModuleManifestRepository::Entry::NounConstraint {
  std::string name;
  std::vector<std::string> types;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_