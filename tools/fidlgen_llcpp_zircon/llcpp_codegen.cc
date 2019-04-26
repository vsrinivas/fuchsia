// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>

#include "llcpp_codegen.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"

namespace fs = std::filesystem;

namespace {

[[noreturn]] void FatalError(const std::string& info) {
  std::cerr << "Error: " << info << ", errno: " << strerror(errno) << std::endl;
  exit(1);
}

rapidjson::Document ReadMetadata(const fs::path& path) {
  rapidjson::Document metadata;
  std::ifstream contents(path);
  if (contents.fail()) {
    FatalError("Failed to read GN metadata at " + std::string(path));
  }
  rapidjson::IStreamWrapper isw(contents);
  rapidjson::ParseResult parse_result = metadata.ParseStream(isw);
  if (!parse_result) {
    FatalError("Failed to parse " + std::string(path) + ", " +
               rapidjson::GetParseError_En(parse_result.Code()) + ", offset " +
               std::to_string(parse_result.Offset()));
  }
  if (!metadata.IsArray()) {
    FatalError("Metadata is not an array");
  }
  return metadata;
}

struct Target {
  fs::path gen_dir;
  std::vector<fs::path> fidl_sources;
  std::vector<std::string> args;
};

std::vector<Target> AllTargets(const fs::path& zircon_build_root) {
  std::vector<Target> targets_vector;
  const auto metadata = ReadMetadata(zircon_build_root / "fidl_gen.json");
  for (auto value_it = metadata.Begin();
       value_it != metadata.End(); ++value_it) {
    const auto& target = *value_it;
    const rapidjson::Value& args = target["args"];
    if (!args.IsArray()) {
      FatalError("args in metadata JSON must be an array");
    }
    std::vector<std::string> args_vector;
    for (auto arg_it = args.Begin(); arg_it != args.End(); ++arg_it) {
      args_vector.emplace_back(arg_it->GetString());
    }
    const rapidjson::Value& fidl_sources = target["fidl_sources"];
    if (!fidl_sources.IsArray()) {
      FatalError("fidl_sources in metadata JSON must be an array");
    }
    std::vector<fs::path> fidl_sources_vector;
    for (auto it = fidl_sources.Begin(); it != fidl_sources.End(); ++it) {
      fidl_sources_vector.emplace_back(fs::path(it->GetString()));
    }
    targets_vector.push_back(Target {
      .gen_dir = fs::path(target["target_gen_dir"].GetString()),
      .args = std::move(args_vector),
      .fidl_sources = std::move(fidl_sources_vector)
    });
  }
  return targets_vector;
}

// Run a command with the specified command, working directory, and arguments.
void RunCommand(const std::string& cmd, const std::string& working_directory,
                std::vector<std::string> args) {
  pid_t pid = fork();
  int status;
  switch (pid) {
  case -1:
    FatalError("Failed to fork");
  case 0: {
    status = chdir(working_directory.c_str());
    if (status != 0) {
      FatalError("Failed to chdir to " + working_directory);
    }
    std::vector<char *> c_args;
    c_args.push_back(const_cast<char *>(cmd.c_str()));
    for (const auto& arg : args) {
      c_args.push_back(const_cast<char *>(arg.c_str()));
    }
    c_args.push_back(nullptr);
    execv(cmd.c_str(), &c_args[0]);
    FatalError("when executing " + cmd + ", execv should not return");
  }
  default:
    pid_t ret_pid = waitpid(pid, &status, 0);
    if (pid != ret_pid) {
      FatalError("when executing " + cmd +
                 ", unexpected return value from waitpid: " +
                 std::to_string(ret_pid));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      FatalError(cmd + " returned an error: " + std::to_string(status));
    }
  }
}

} // namespace

bool DoValidate(std::filesystem::path zircon_build_root,
                std::filesystem::path fidlgen_llcpp_path,
                std::vector<fs::path>* out_dependencies) {
  auto all_targets = AllTargets(zircon_build_root);
  for (const auto& target : all_targets) {
    for (const auto& source : target.fidl_sources) {
      out_dependencies->push_back(zircon_build_root / source);
    }
    // TODO(yifeit): Implement.
  }
  return true;
}

void DoUpdate(fs::path zircon_build_root,
              fs::path fidlgen_llcpp_path,
              std::vector<fs::path>* out_dependencies) {
  const auto all_targets = AllTargets(zircon_build_root);
  for (const auto& target : all_targets) {
    for (const auto& source : target.fidl_sources) {
      out_dependencies->push_back(zircon_build_root / source);
    }
    RunCommand(fidlgen_llcpp_path, zircon_build_root, target.args);
  }
}
