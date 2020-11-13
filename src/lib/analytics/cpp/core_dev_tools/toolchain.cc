// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/toolchain.h"

#include <limits.h>

#include <cstring>
#include <filesystem>
#include <string_view>

#include "rapidjson/document.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace analytics {

namespace {

// For the case when we fail to decide the toolchain version
constexpr char kUnknownVersion[] = "unknown";

// For the case when detection of version is not supported for the toolchain
constexpr char kNaVersion[] = "NA";

// Copied from //src/developer/debug/zxdb/common/host_util.cc to avoid dependency on zxdb
// Returns the path (dir + file name) of the current executable.
std::string GetSelfPath() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(&result[0], &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
#error Write this for your platform.
#endif

  char fullpath[PATH_MAX];
  return std::string(realpath(result.c_str(), fullpath));
}

std::string ReadInTreeVersion(const std::filesystem::path& version_path) {
  std::string version;
  if (!files::ReadFileToString(version_path, &version)) {
    return kUnknownVersion;
  }
  return std::string(fxl::TrimString(version, "\n"));
}

std::string ReadSdkVersion(const std::filesystem::path& version_path) {
  std::string json;
  if (!files::ReadFileToString(version_path, &json)) {
    return kUnknownVersion;
  }
  rapidjson::Document document;
  document.Parse(json);
  if (!document.HasMember("id")) {
    return kUnknownVersion;
  }
  const auto& id = document["id"];
  return std::string(id.GetString(), id.GetStringLength());
}

}  // namespace

ToolchainInfo GetToolchainInfo() {
  ToolchainInfo toolchain_info;

  auto path_string = GetSelfPath();
  std::filesystem::path path(path_string);
  std::error_code _ignore;
  do {
    auto version_path = path / "gen" / "latest-commit-date.txt";
    if (std::filesystem::exists(version_path, _ignore)) {
      toolchain_info.toolchain = Toolchain::kInTree;
      toolchain_info.version = ReadInTreeVersion(version_path);
      return toolchain_info;
    }

    version_path = path / "meta" / "manifest.json";
    if (std::filesystem::exists(version_path, _ignore)) {
      toolchain_info.toolchain = Toolchain::kSdk;
      toolchain_info.version = ReadSdkVersion(version_path);
      return toolchain_info;
    }

    path = path.parent_path();
  } while (!std::filesystem::equivalent(path, "/"));

  toolchain_info.toolchain = Toolchain::kOther;
  toolchain_info.version = kNaVersion;
  return toolchain_info;
}

std::string ToString(Toolchain toolchain) {
  switch (toolchain) {
    case Toolchain::kInTree:
      return "in-tree";
    case Toolchain::kSdk:
      return "sdk";
    case Toolchain::kOther:
      return "other";
  }
}

}  // namespace analytics
