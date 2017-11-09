// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_SOURCE_MODULE_MANIFEST_SOURCE_H_
#define PERIDOT_LIB_MODULE_MANIFEST_SOURCE_MODULE_MANIFEST_SOURCE_H_

#include <functional>
#include <string>
#include <vector>

#include "lib/fxl/tasks/task_runner.h"

namespace modular {

// Abstract base class for all Module Manifest Source implementations.
class ModuleManifestSource {
 public:
  struct Entry;
  using NewEntryFn = std::function<void(std::string, Entry)>;
  using RemovedEntryFn = std::function<void(std::string)>;
  using IdleFn = std::function<void()>;

  virtual ~ModuleManifestSource() = 0;

  // Watches the source for new or removed Module manifest entries.  Posts
  // tasks to |task_runner| calling |new_fn| for each entry in new files and
  // |removed_fn| for entries that have been removed. Calls |idle_fn| once all
  // existing entries at the time of calling Watch() have been read and sent to
  // |new_fn|.
  //
  // Destroying |this| stops watching and guarantees neither |new_fn| or
  // |removed_fn| will be called. Caller is responsible for ensuring
  // |task_runner| remains alive as long as |this| is alive.
  //
  // |new_fn| takes a string Entry id and the Entry itself.
  //
  // |removed_fn| takes only the string Entry id.
  virtual void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
                     IdleFn idle_fn,
                     NewEntryFn new_fn,
                     RemovedEntryFn removed_fn) = 0;
};

// These fields mirror those in the Module manifest doc:
// https://fuchsia.googlesource.com/peridot/+/HEAD/docs/modular/manifests/module.md
struct ModuleManifestSource::Entry {
  struct NounConstraint;

  std::string binary;
  std::string local_name;
  std::string verb;
  std::vector<NounConstraint> noun_constraints;
};

struct ModuleManifestSource::Entry::NounConstraint {
  std::string name;
  std::vector<std::string> types;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_SOURCE_MODULE_MANIFEST_SOURCE_H_