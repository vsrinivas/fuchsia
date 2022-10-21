// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"

#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "src/lib/analytics/cpp/metric_properties/optional_path.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace analytics::metric_properties {

namespace {

std::filesystem::path GetMetricBaseDirectory() {
#if defined(__APPLE__)
  auto home = internal::GetOptionalPathFromEnv("HOME");
  FX_DCHECK(home.has_value());
  return *home / "Library" / "Application Support";
#else
  auto dir = internal::GetOptionalPathFromEnv("XDG_DATA_HOME");
  if (dir.has_value()) {
    return *dir;
  }
  dir = internal::GetOptionalPathFromEnv("HOME");
  FX_DCHECK(dir.has_value());
  return *dir / ".local" / "share";
#endif
}

std::filesystem::path GetMetricPropertiesDirectory() {
  return GetMetricBaseDirectory() / "Fuchsia" / "metrics";
}

std::filesystem::path GetOldMetricPropertiesDirectory() {
  auto path = internal::GetOptionalPathFromEnv("HOME");
  FX_DCHECK(path.has_value());
  return *path / ".fuchsia" / "metrics";
}

std::filesystem::path GetMetricPropertyPath(std::string_view name) {
  return GetMetricPropertiesDirectory() / name;
}

}  // namespace

std::optional<std::string> Get(std::string_view name) {
  auto path = GetMetricPropertyPath(name);

  std::string data;
  if (!files::ReadFileToString(path, &data)) {
    return std::optional<std::string>();
  }
  return std::string(fxl::TrimString(data, "\n"));
}

void Set(std::string_view name, std::string_view value) {
  auto property_directory = GetMetricPropertiesDirectory();
  bool success = files::CreateDirectory(property_directory);
  if (!success)
    return;
  auto property_file = property_directory / name;

  std::string data(value);
  data += "\n";

  success = files::WriteFile(property_file, data);
  if (!success) {
    std::cerr << "Warning: unable to set analytics property " << name << std::endl;
  }
}

std::optional<bool> GetBool(std::string_view name) {
  auto result = Get(name);
  if (!result.has_value()) {
    return std::optional<bool>();
  }
  return *result == "1";
}

void Delete(std::string_view name) {
  auto path = GetMetricPropertyPath(name);
  std::error_code _ignore;
  std::filesystem::remove(path, _ignore);
}

bool Exists(std::string_view name) {
  auto path = GetMetricPropertyPath(name);
  std::error_code _ignore;
  return std::filesystem::exists(path, _ignore);
}

void MigrateMetricDirectory() {
  auto path = GetMetricPropertiesDirectory();
  std::error_code _ignore;
  if (std::filesystem::exists(path, _ignore)) {
    // no need to migrate as the new folder already exists
    return;
  }

  auto old_path = GetOldMetricPropertiesDirectory();
  if (!std::filesystem::is_directory(old_path, _ignore)) {
    // no need to migrate as the old folder does not exist
    return;
  }

  std::error_code ec;
  if (files::CreateDirectory(path.parent_path())) {
    std::filesystem::rename(old_path, path, ec);
    if (!ec) {
      std::filesystem::create_directory_symlink(path, old_path, ec);
    }
  }
}

}  // namespace analytics::metric_properties
