// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_SOURCE_MODULE_MANIFEST_SOURCE_H_
#define PERIDOT_LIB_MODULE_MANIFEST_SOURCE_MODULE_MANIFEST_SOURCE_H_

#include <functional>
#include <string>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/dispatcher.h>

namespace modular {

// Abstract base class for all Module Manifest Source implementations.
class ModuleManifestSource {
 public:
  using NewEntryFn =
      std::function<void(std::string, fuchsia::modular::ModuleManifest)>;
  using RemovedEntryFn = std::function<void(std::string)>;
  using IdleFn = std::function<void()>;

  virtual ~ModuleManifestSource() = 0;

  // Watches the source for new or removed Module manifest entries.  Posts
  // tasks to |dispatcher| calling |new_fn| for each entry in new files and
  // |removed_fn| for entries that have been removed. Calls |idle_fn| once all
  // existing entries at the time of calling Watch() have been read and sent to
  // |new_fn|.
  //
  // Destroying |this| stops watching and guarantees neither |new_fn| or
  // |removed_fn| will be called. Caller is responsible for ensuring
  // |dispatcher| remains alive as long as |this| is alive.
  //
  // |new_fn| takes a string Entry id and the Entry itself.
  //
  // |removed_fn| takes only the string Entry id.
  virtual void Watch(async_dispatcher_t* dispatcher, IdleFn idle_fn, NewEntryFn new_fn,
                     RemovedEntryFn removed_fn) = 0;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_SOURCE_MODULE_MANIFEST_SOURCE_H_
