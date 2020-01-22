// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
  std::string name;
  std::vector<fs::path> fidl_sources;
  std::vector<std::string> args;
  fs::path json;
  fs::path header;
  fs::path source;
  fs::path include_base;
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
      .name = target["name"].GetString(),
      .fidl_sources = std::move(fidl_sources_vector),
      .args = std::move(args_vector),
      .json = fs::path(target["json"].GetString()),
      .header = fs::path(target["header"].GetString()),
      .source = fs::path(target["source"].GetString()),
      .include_base = fs::path(target["include_base"].GetString()),
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

fs::path FindCommonPath(fs::path a, fs::path b) {
  auto a_it = a.begin();
  auto b_it = b.begin();
  fs::path result;
  while (a_it != a.end() && b_it != b.end()) {
    auto& a_part = *a_it;
    auto& b_part = *b_it;
    if (a_part != b_part) {
      break;
    }
    result /= a_part;
    a_it++;
    b_it++;
  }
  return result;
}

bool Diff(fs::path a, fs::path b) {
  std::ifstream a_stream(a.c_str(), std::ios::binary | std::ios::ate);
  std::ifstream b_stream(b.c_str(), std::ios::binary | std::ios::ate);
  if (!a_stream) {
    return false;
  }
  if (!b_stream) {
    return false;
  }
  if (a_stream.tellg() != b_stream.tellg()) {
    return false;
  }
  a_stream.seekg(0, std::ifstream::beg);
  b_stream.seekg(0, std::ifstream::beg);
  return std::equal(std::istreambuf_iterator<char>(a_stream.rdbuf()),
                    std::istreambuf_iterator<char>(),
                    std::istreambuf_iterator<char>(b_stream.rdbuf()));
}

} // namespace

bool DoValidate(std::filesystem::path zircon_build_root,
                std::filesystem::path fidlgen_llcpp_path,
                std::filesystem::path tmp_dir,
                std::vector<fs::path>* out_dependencies) {
  fs::remove_all(tmp_dir);
  fs::create_directories(tmp_dir);
  auto all_targets = AllTargets(zircon_build_root);
  auto normalize = [&zircon_build_root](fs::path path) {
    return fs::weakly_canonical(zircon_build_root / path);
  };
  for (const auto& target : all_targets) {
    for (const auto& source : target.fidl_sources) {
      out_dependencies->push_back(zircon_build_root / source);
    }
    fs::path json = normalize(target.json);
    fs::path header = normalize(target.header);
    fs::path source = normalize(target.source);
    fs::path include_base = normalize(target.include_base);
    fs::path common = FindCommonPath(header,
                                     FindCommonPath(include_base, source));
    // Generate in an alternative location
    fs::path tmp = fs::absolute(tmp_dir) / target.name;
    fs::path alt_header = tmp / fs::relative(header, common);
    fs::path alt_source = tmp / fs::relative(source, common);
    fs::path alt_include_base = tmp / fs::relative(include_base, common);
    std::vector<std::string> args = {
      "-json",
      json,
      "-include-base",
      alt_include_base,
      "-header",
      alt_header,
      "-source",
      alt_source
    };
    RunCommand(fidlgen_llcpp_path, zircon_build_root, args);
    if (!Diff(header, alt_header)) {
      std::cerr << header << " is different from " << alt_header << std::endl;
      return false;
    }
    if (!Diff(source, alt_source)) {
      std::cerr << source << " is different from " << alt_source << std::endl;
      return false;
    }
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
    std::cout << "Generating low-level C++ bindings for " << target.name
              << std::endl;
    RunCommand(fidlgen_llcpp_path, zircon_build_root, target.args);
  }
}
