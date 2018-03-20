// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/push_package_source.h"

#include <fs/service.h>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/module_manifest_source/json.h"

namespace modular {

PushPackageSource::PushPackageSource(
    component::ApplicationContext* const context)
    : weak_factory_(this) {
  context->debug_export_dir()->AddEntry(
      PushPackageIndexer::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        indexer_bindings_.AddBinding(
            this,
            f1dl::InterfaceRequest<PushPackageIndexer>(std::move(channel)));
        return ZX_OK;
      })));
}

PushPackageSource::~PushPackageSource() {}

void PushPackageSource::IndexManifest(
    const f1dl::StringPtr& package_name,
    const f1dl::StringPtr& module_manifest_path) {
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(new_entry_fn_);

  std::string data;
  if (!files::ReadFileToString(module_manifest_path.get(), &data)) {
    FXL_LOG(ERROR) << "Couldn't read module manifest from: "
                   << module_manifest_path.get();
    return;
  }

  auto entry = modular::ModuleManifest::New();
  if (!ModuleManifestEntryFromJson(data, &entry)) {
    FXL_LOG(WARNING) << "Couldn't parse module manifest from: "
                     << module_manifest_path;
    return;
  }

  task_runner_->PostTask(
      fxl::MakeCopyable([weak_this = weak_factory_.GetWeakPtr(), package_name,
                         entry = std::move(entry)]() mutable {
        if (!weak_this) {
          return;
        }

        weak_this->new_entry_fn_(package_name, std::move(entry));
      }));
}

void PushPackageSource::Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
                              IdleFn idle_fn,
                              NewEntryFn new_fn,
                              RemovedEntryFn removed_fn) {
  task_runner_ = task_runner;
  new_entry_fn_ = new_fn;
  idle_fn();
}

}  // namespace modular
