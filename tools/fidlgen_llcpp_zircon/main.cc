// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string.h>
#include <utility>

#include "llcpp_codegen.h"

namespace fs = std::filesystem;

static void Usage(const char* exe_name) {
  fprintf(
      stderr,
      "Generate or validate the checked-in low-level C++ bindings in zircon.\n"
      "Usage: %s (validate|update) ZIRCON_BUILDROOT FIDLGEN_LLCPP_PATH "
      "STAMP DEPFILE\n"
      "ZIRCON_BUILDROOT is the root build directory of the Zircon GN build.\n"
      "FIDLGEN_LLCPP_PATH is the path to the fidlgen_llcpp executable.\n"
      "STAMP is the output path to a file indicating the success of the tool.\n"
      "DEPFILE is the output path to a depfile describing the FIDL files\n"
      "which when updated should trigger a re-run of this tool.\n"
      "\n"
      "When validate is specified, it will validate that the generated\n"
      "bindings are up-to-date, exiting with an error if not so.\n"
      "Files in the source tree are not modified.\n"
      "\n"
      "When update is specified, it will regenerate the bindings in\n"
      "zircon/system/fidl from GN metadata.\n"
      "\n",
      exe_name);
}

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << argv[0] << ": Invalid arguments" << "\n\n";
    Usage(argv[0]);
    return -1;
  }
  // Since we're dealing with two builds, it's less ambiguous if we start with
  // all absolute paths in the beginning, then convert to relative paths
  // where required, similar to rebase_path in GN.
  fs::path zircon_build_root = fs::absolute(argv[2]);
  fs::path fidlgen_llcpp_path = fs::absolute(argv[3]);
  fs::path stamp_path = fs::absolute(argv[4]);
  fs::remove(stamp_path);
  fs::path depfile_path = fs::absolute(argv[5]);
  fs::remove(depfile_path);

  std::vector<fs::path> dependencies;
  if (strcmp(argv[1], "validate") == 0) {
    DoValidate(zircon_build_root, fidlgen_llcpp_path, &dependencies);
  } else if (strcmp(argv[1], "update") == 0) {
    DoUpdate(zircon_build_root, fidlgen_llcpp_path, &dependencies);
  } else {
    std::cerr << argv[0] << ": Expected validate or update, not " << argv[1]
              << "\n\n";
    Usage(argv[0]);
    return -1;
  }
  // Generate stamp file
  std::fstream stamp;
  stamp.open(stamp_path, std::ios::out);
  if (!stamp) {
    std::cerr << "Failed to stamp " << stamp_path << std::endl;
    return -1;
  }
  stamp.close();
  // Generate depfile
  std::fstream depfile;
  depfile.open(depfile_path, std::ios::out);
  if (!depfile) {
    std::cerr << "Failed to create depfile " << depfile_path << std::endl;
    return -1;
  }
  depfile << fs::relative(stamp_path).string() << ":";
  for (const auto& dep : dependencies) {
    depfile << " " << fs::relative(dep).string();
  }
  depfile << std::endl;
  depfile.close();
  return 0;
}