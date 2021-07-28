// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/manifest_parser.h"

#include <lib/zx/status.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace {

const std::string kFuchsiaPkgPrefix = "fuchsia-pkg://";
const std::string kFuchsiaBootPrefix = "fuchsia-boot://";

bool IsFuchsiaPkgScheme(std::string_view url) {
  return url.compare(0, kFuchsiaPkgPrefix.length(), kFuchsiaPkgPrefix) == 0;
}

zx::status<std::string> GetResourcePath(std::string_view url) {
  size_t seperator = url.find('#');
  if (seperator == std::string::npos) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }
  return zx::ok(url.substr(seperator + 1));
}

}  // namespace

bool IsFuchsiaBootScheme(std::string_view url) {
  return url.compare(0, kFuchsiaBootPrefix.length(), kFuchsiaBootPrefix) == 0;
}

zx::status<std::string> GetBasePathFromUrl(const std::string& url) {
  if (IsFuchsiaPkgScheme(url)) {
    component::FuchsiaPkgUrl package_url;
    if (!package_url.Parse(url)) {
      LOGF(ERROR, "Failed to parse fuchsia url: %s", url.c_str());
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok(fxl::Substitute("/pkgfs/packages/$0/$1", package_url.package_name(),
                                  package_url.variant()));
  }
  if (IsFuchsiaBootScheme(url)) {
    auto resource_path = GetResourcePath(url);
    if (resource_path.is_error()) {
      LOGF(ERROR, "Failed to parse boot url: %s", url.c_str());
      return resource_path;
    }
    return zx::ok("/boot");
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<std::string> GetPathFromUrl(const std::string& url) {
  if (IsFuchsiaPkgScheme(url)) {
    component::FuchsiaPkgUrl package_url;
    if (!package_url.Parse(url)) {
      LOGF(ERROR, "Failed to parse fuchsia url: %s", url.c_str());
      return zx::error(ZX_ERR_INTERNAL);
    }
    return zx::ok(fxl::Substitute("/pkgfs/packages/$0/$1/$2", package_url.package_name(),
                                  package_url.variant(), package_url.resource_path()));
  }
  if (IsFuchsiaBootScheme(url)) {
    auto resource_path = GetResourcePath(url);
    if (resource_path.is_error()) {
      LOGF(ERROR, "Failed to parse boot url: %s", url.c_str());
      return resource_path;
    }
    return zx::ok("/boot/" + resource_path.value());
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<DriverManifestEntries> ParseDriverManifest(rapidjson::Document manifest) {
  DriverManifestEntries parsed_drivers;

  if (!manifest.IsArray()) {
    LOGF(ERROR, "Driver Manifest's top-level object is not an array");
    return zx::error(ZX_ERR_INTERNAL);
  }

  for (size_t i = 0; i < manifest.Size(); i++) {
    const auto& driver = manifest[i];
    if (!driver.IsObject()) {
      continue;
    }
    if (manifest[i].HasMember("driver_url")) {
      const auto& driver_url = manifest[i]["driver_url"];
      if (driver_url.IsString()) {
        DriverManifestEntry entry;
        entry.driver_url = driver_url.GetString();
        parsed_drivers.push_back(std::move(entry));
      }
    }
  }

  return zx::ok(std::move(parsed_drivers));
}

zx::status<DriverManifestEntries> ParseDriverManifestFromPath(const std::string& path) {
  json_parser::JSONParser parser;
  rapidjson::Document manifest = parser.ParseFromFile(path);
  if (parser.HasError()) {
    LOGF(ERROR, "DriverManifest JSON failed to parse: %s", parser.error_str().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }

  return ParseDriverManifest(std::move(manifest));
}
