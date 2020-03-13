// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/module_manifest_source/module_package_source.h"

#include <dirent.h>
#include <lib/async/cpp/task.h>
#include <lib/vfs/cpp/service.h>
#include <sys/types.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/module_manifest_source/json.h"

namespace modular {

using ::fuchsia::maxwell::internal::ModulePackageIndexer;

ModulePackageSource::ModulePackageSource(sys::ComponentContext* const context)
    : weak_factory_(this) {
  context->outgoing()->debug_dir()->AddEntry(
      ModulePackageIndexer::Name_,
      std::make_unique<vfs::Service>([this](zx::channel channel, async_dispatcher_t*) {
        indexer_bindings_.AddBinding(
            this, fidl::InterfaceRequest<ModulePackageIndexer>(std::move(channel)));
      }));
}

ModulePackageSource::~ModulePackageSource() {}

void ModulePackageSource::IndexManifest(std::string package_name,
                                        std::string module_manifest_path) {
  FX_DCHECK(dispatcher_);
  FX_DCHECK(new_entry_fn_);

  std::string data;
  if (!files::ReadFileToString(module_manifest_path, &data)) {
    FX_LOGS(ERROR) << "Couldn't read module manifest from: " << module_manifest_path;
    return;
  }

  fuchsia::modular::ModuleManifest entry;
  if (!ModuleManifestEntryFromJson(data, &entry)) {
    FX_LOGS(WARNING) << "Couldn't parse module manifest from: " << module_manifest_path;
    return;
  }

  async::PostTask(dispatcher_, [weak_this = weak_factory_.GetWeakPtr(), package_name,
                                entry = std::move(entry)]() mutable {
    if (!weak_this) {
      return;
    }

    weak_this->new_entry_fn_(entry.binary, std::move(entry));
  });
}

// TODO(vardhan): Move this into garnet's fxl.
void IterateDirectory(fxl::StringView dirname, fit::function<void(fxl::StringView)> callback) {
  DIR* fd = opendir(dirname.data());
  if (fd == nullptr) {
    perror("Could not open module package index directory: ");
    return;
  }
  struct dirent* dp = nullptr;
  while ((dp = readdir(fd)) != nullptr) {
    if (dp->d_name[0] != '.') {
      callback(dp->d_name);
    }
  }
  closedir(fd);
}

void ModulePackageSource::Watch(async_dispatcher_t* dispatcher, IdleFn idle_fn, NewEntryFn new_fn,
                                RemovedEntryFn removed_fn) {
  dispatcher_ = dispatcher;
  new_entry_fn_ = std::move(new_fn);

  idle_fn();
}

}  // namespace modular
