// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fdio/util.h>
#include <sys/types.h>

#include <fuchsia/cpp/module_manifest_source.h>
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/strings/string_printf.h"
#include "peridot/lib/module_manifest_source/package_util.h"

// This function finds the ModulePackageIndexer fidl service that the
// module_resolver runs.
std::string FindModulePackageIndexerService() {
  // The ModulePackageIndexer service is run by the module_resolver process
  // under the "user-*" job name.  The structured path of to this service is:
  // "/hub/sys/<user job
  // name>/module_resolver/debug/modular.ModulePackageIndexer"

  // Here, we go through /hub/sys and find the user's job name, which always
  // begins with 'user-'.
  DIR* fd = opendir("/hub/sys");
  struct dirent* dp = NULL;
  std::string user_env;
  while ((dp = readdir(fd)) != NULL) {
    if (strstr(dp->d_name, "user-") == dp->d_name) {
      user_env = dp->d_name;
      break;
    }
  }
  closedir(fd);

  FXL_CHECK(!user_env.empty()) << "Could not find the running user's job.";
  return fxl::StringPrintf("/hub/sys/%s/module_resolver/debug/%s",
                           user_env.c_str(),
                           module_manifest_source::ModulePackageIndexer::Name_);
}

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);

  if (command_line.positional_args().size() != 2) {
    std::cerr << "Usage:  " << command_line.argv0()
              << " <package name> <version>";
    return 1;
  }

  auto service_path = FindModulePackageIndexerService();

  module_manifest_source::ModulePackageIndexerPtr indexer;
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
