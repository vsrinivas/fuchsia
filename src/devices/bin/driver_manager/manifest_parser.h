// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_MANIFEST_PARSER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_MANIFEST_PARSER_H_

#include <lib/zx/status.h>

#include "src/devices/lib/log/log.h"
#include "src/lib/json_parser/json_parser.h"

zx::status<std::string> GetPathFromUrl(const std::string& url);

struct DriverManifestEntry {
  std::string driver_url;
};

using DriverManifestEntries = std::vector<DriverManifestEntry>;

zx::status<DriverManifestEntries> ParseDriverManifestFromPath(const std::string& path);
zx::status<DriverManifestEntries> ParseDriverManifest(rapidjson::Document manifest);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_MANIFEST_PARSER_H_
