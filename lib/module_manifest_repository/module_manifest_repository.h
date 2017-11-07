// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_
#define PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "garnet/public/lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace modular {

// Watches a directory for new Module manifest files, parses them, and notifies
// each watcher of each entry within.
class ModuleManifestRepository {
 public:
  struct Entry;
  using NewEntryFn = std::function<void(std::string, Entry)>;
  using RemovedEntryFn = std::function<void(std::string)>;

  // If |repository_dir| does not already exist, it will be created.
  ModuleManifestRepository(std::string repository_dir);

  // Watches |repository_dir| (in the constructor) for Module manifest files,
  // parses them and posts tasks to |task_runner| calling |new_fn| for each
  // entry in new files and |removed_fn| for entries that have been removed.
  //
  // |new_fn| takes a string Entry id and the Entry itself.
  //
  // |removed_fn| takes only the string Entry id.
  void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
             NewEntryFn new_fn,
             RemovedEntryFn removed_fn);
  ~ModuleManifestRepository();

 private:
  void OnNewFile(const std::string& name, NewEntryFn);
  void OnRemoveFile(const std::string& name, RemovedEntryFn);

  const std::string repository_dir_;
  NewEntryFn new_entry_fn_;
  RemovedEntryFn removed_entry_fn_;

  // Map of file path to entry IDs. Supports OnRemoveFile.
  std::map<std::string, std::vector<std::string>> file_entry_ids_;

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