// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_PUSH_PACKAGE_SOURCE_H_
#define PERIDOT_LIB_MODULE_MANIFEST_PUSH_PACKAGE_SOURCE_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "garnet/public/lib/app/cpp/application_context.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"
#include "peridot/lib/module_manifest_source/fidl/push_package_indexer.fidl.h"
#include "peridot/lib/module_manifest_source/module_manifest_source.h"

namespace modular {

class PushPackageSource : public ModuleManifestSource, PushPackageIndexer {
 public:
  PushPackageSource(component::ApplicationContext* context);
  ~PushPackageSource() override;

  // |ModuleManifestSource|
  void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
             IdleFn idle_fn,
             NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override;

 private:
  // |PushPackageIndexer|
  void IndexManifest(const f1dl::StringPtr& package_name,
                     const f1dl::StringPtr& module_manifest_path) override;

  const std::string dir_;
  NewEntryFn new_entry_fn_;

  f1dl::BindingSet<PushPackageIndexer> indexer_bindings_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  fxl::WeakPtrFactory<PushPackageSource> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PushPackageSource);
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_PUSH_PACKAGE_SOURCE_H_
