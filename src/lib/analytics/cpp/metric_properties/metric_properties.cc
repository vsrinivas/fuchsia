// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"

#include <lib/syslog/cpp/macros.h>

#include <cstdlib>
#include <filesystem>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace analytics::metric_properties {

namespace {

std::filesystem::path GetFuchsiaDataDirectory() {
  std::filesystem::path fuchsia_data_dir(std::getenv("HOME"));
  FX_DCHECK(!fuchsia_data_dir.empty());
  fuchsia_data_dir /= ".fuchsia";
  return fuchsia_data_dir;
}

std::filesystem::path GetMetricPropertiesDirectory() {
  return GetFuchsiaDataDirectory() / "metrics";
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
  auto path = GetMetricPropertiesDirectory();
  bool success = files::CreateDirectory(path);
  if (!success)
    return;
  path /= name;

  std::string data(value);
  data += "\n";

  // pass "" as temp_root to use the global tmp directory.
  files::WriteFileInTwoPhases(path, data, "");
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

}  // namespace analytics::metric_properties
