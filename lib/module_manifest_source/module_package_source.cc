// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest_source/module_package_source.h"

#include <dirent.h>
#include <sys/types.h>

#include <fs/service.h>
#include <lib/async/cpp/task.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <lib/fxl/strings/split_string.h>
#include <lib/fxl/strings/string_printf.h>

#include "peridot/lib/module_manifest_source/json.h"
#include "peridot/lib/module_manifest_source/package_util.h"

namespace modular {
namespace {
// NOTE: This must match the path specified in
// //peridot/build/module_manifest.gni
constexpr char kInitialModulePackagesIndexDir[] =
    "/system/data/initial_module_packages";
}  // namespace

using ::fuchsia::maxwell::internal::ModulePackageIndexer;

ModulePackageSource::ModulePackageSource(
    component::StartupContext* const context)
    : weak_factory_(this) {
  context->outgoing().debug_dir()->AddEntry(
      ModulePackageIndexer::Name_,
      fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
        indexer_bindings_.AddBinding(
            this,
            fidl::InterfaceRequest<ModulePackageIndexer>(std::move(channel)));
        return ZX_OK;
      })));
}

ModulePackageSource::~ModulePackageSource() {}

void ModulePackageSource::IndexManifest(fidl::StringPtr package_name,
                                        fidl::StringPtr module_manifest_path) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(new_entry_fn_);

  std::string data;
  if (!files::ReadFileToString(module_manifest_path.get(), &data)) {
    FXL_LOG(ERROR) << "Couldn't read module manifest from: "
                   << module_manifest_path.get();
    return;
  }

  fuchsia::modular::ModuleManifest entry;
  if (!ModuleManifestEntryFromJson(data, &entry)) {
    FXL_LOG(WARNING) << "Couldn't parse module manifest from: "
                     << module_manifest_path;
    return;
  }

  async::PostTask(
      dispatcher_,
      fxl::MakeCopyable([weak_this = weak_factory_.GetWeakPtr(), package_name,
                         entry = std::move(entry)]() mutable {
        if (!weak_this) {
          return;
        }

        weak_this->new_entry_fn_(package_name, std::move(entry));
      }));
}

// TODO(vardhan): Move this into garnet's fxl.
void IterateDirectory(fxl::StringView dirname,
                      std::function<void(fxl::StringView)> callback) {
  DIR* fd = opendir(dirname.data());
  if (fd == NULL) {
    perror("Could not open module package index directory: ");
    return;
  }
  struct dirent* dp = NULL;
  while ((dp = readdir(fd)) != NULL) {
    if (dp->d_name[0] != '.') {
      callback(dp->d_name);
    }
  }
  closedir(fd);
}

void ModulePackageSource::Watch(async_dispatcher_t* dispatcher, IdleFn idle_fn,
                                NewEntryFn new_fn, RemovedEntryFn removed_fn) {
  dispatcher_ = dispatcher;
  new_entry_fn_ = new_fn;

  IterateDirectory(
      kInitialModulePackagesIndexDir, [this](fxl::StringView filename) {
        std::string contents;
        if (!files::ReadFileToString(
                fxl::StringPrintf("%s/%s", kInitialModulePackagesIndexDir,
                                  filename.data()),
                &contents)) {
          FXL_LOG(ERROR) << "Could not read module package index: "
                         << filename.data();
        }
        auto module_pkgs = fxl::SplitString(
            contents, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
        for (auto module_pkg_view : module_pkgs) {
          auto module_pkg = module_pkg_view.ToString();
          // TODO(vardhan): We only index module package with version=0.
          IndexManifest(module_pkg,
                        GetModuleManifestPathFromPackage(module_pkg, "0"));
        }
      });

  idle_fn();
}

}  // namespace modular
