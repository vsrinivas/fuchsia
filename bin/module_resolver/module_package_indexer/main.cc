// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <iostream>
#include <string>
#include <vector>

#include <fuchsia/maxwell/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/util.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/strings/string_printf.h"
#include "peridot/lib/module_manifest_source/package_util.h"

using ::fuchsia::maxwell::internal::ModulePackageIndexer;
using ::fuchsia::maxwell::internal::ModulePackageIndexerPtr;

// This function finds the ModulePackageIndexer fidl service that the
// module_resolver runs.
std::string FindModulePackageIndexerService() {
  // The ModulePackageIndexer service is run by the module_resolver component
  // under the "user-*" realm. The structured path of to this service is:
  // /hub/r/sys/<koid>/r/user-<userid>/<koid>/c/module_resolver/<koid>/out/debug
  auto glob_str = fxl::StringPrintf(
      "/hub/r/sys/*/r/user-*/*/c/module_resolver/*/out/"
      "debug/%s", ModulePackageIndexer::Name_);

  glob_t globbuf;
  std::string service_path;
  FXL_CHECK(glob(glob_str.data(), 0, NULL, &globbuf) == 0);
  if (globbuf.gl_pathc > 0) {
    service_path = globbuf.gl_pathv[0];
  }
  if (globbuf.gl_pathc > 1) {
    FXL_DLOG(WARNING) << "Found more than one module resolver.";
  }
  globfree(&globbuf);
  return service_path;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  if (command_line.positional_args().size() != 2) {
    std::cerr << "Usage:  " << command_line.argv0()
              << " <package name> <version>";
    return 1;
  }

  auto service_path = FindModulePackageIndexerService();
  FXL_CHECK(!service_path.empty())
      << "Could not find a running module resolver. Is the user logged in?";

  ModulePackageIndexerPtr indexer;
  auto req_handle = indexer.NewRequest().TakeChannel();
  if (fdio_service_connect(service_path.c_str(), req_handle.get()) != ZX_OK) {
    FXL_LOG(FATAL) << "Could not connect to service " << service_path;
    return 1;
  }

  const auto& package_name = command_line.positional_args()[0];
  const auto& package_version = command_line.positional_args()[1];
  indexer->IndexManifest(
      package_name,
      modular::GetModuleManifestPathFromPackage(package_name, package_version));

  return 0;
}
