// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_REPOSITORY_DIRECTORY_REPOSITORY_H_
#define PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_REPOSITORY_DIRECTORY_REPOSITORY_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "garnet/public/lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/lib/module_manifest_repository/module_manifest_repository.h"

namespace modular {

// Watches a directory for new Module manifest files, parses them, and notifies
// each watcher of each entry within.
class DirectoryRepository : public ModuleManifestRepository {
 public:
  // If |repository_dir| does not already exist, it will be created.
  DirectoryRepository(std::string repository_dir);
  ~DirectoryRepository() override;

  void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
             NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override;

 private:
  void OnNewFile(const std::string& name, NewEntryFn);
  void OnRemoveFile(const std::string& name, RemovedEntryFn);

  const std::string repository_dir_;
  NewEntryFn new_entry_fn_;
  RemovedEntryFn removed_entry_fn_;

  // Map of file path to entry IDs. Supports OnRemoveFile.
  std::map<std::string, std::vector<std::string>> file_entry_ids_;

  fxl::WeakPtrFactory<DirectoryRepository> weak_factory_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_REPOSITORY_DIRECTORY_REPOSITORY_H_