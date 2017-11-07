// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_
#define PERIDOT_LIB_MODULE_MANIFEST_REPOSITORY_MODULE_MANIFEST_REPOSITORY_H_

#include <functional>
#include <string>
#include <vector>

#include "lib/fxl/tasks/task_runner.h"

namespace modular {

// Abstract base class for all Module Manifest Repository implementations.
class ModuleManifestRepository {
 public:
  struct Entry;
  using NewEntryFn = std::function<void(std::string, Entry)>;
  using RemovedEntryFn = std::function<void(std::string)>;

  virtual ~ModuleManifestRepository() = 0;

  // Watches the repository for new or removed Module manifest entries.  Posts
  // tasks to |task_runner| calling |new_fn| for each entry in new files and
  // |removed_fn| for entries that have been removed.
  //
  // Destroying |this| stops watching and guarantees neither |new_fn| or
  // |removed_fn| will be called. Caller is responsible for ensuring
  // |task_runner| remains alive as long as |this| is alive.
  //
  // |new_fn| takes a string Entry id and the Entry itself.
  //
  // |removed_fn| takes only the string Entry id.
  virtual void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
                     NewEntryFn new_fn,
                     RemovedEntryFn removed_fn) = 0;
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