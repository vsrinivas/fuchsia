// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/config.h"

#include <lib/syslog/cpp/macros.h>

#include <cinttypes>

namespace devmgr {

Config::Options Config::ReadOptions(std::istream& stream) {
  Options options;
  for (std::string line; std::getline(stream, line);) {
    if (line == kDefault) {
      auto default_options = DefaultOptions();
      options.insert(default_options.begin(), default_options.end());
    } else if (line.size() > 0) {
      if (line[0] == '#')
        continue;  // Treat as comment; ignore;

      std::string key, value;
      if (auto separator_index = line.find('='); separator_index != std::string::npos) {
        key = line.substr(0, separator_index);
        value = line.substr(separator_index + 1);
      } else {
        key = line;  // No value, just a bare key.
      }

      if (key[0] == '-') {
        options.erase(key.substr(1));
      } else {
        options[key] = std::move(value);
      }
    }
  }
  return options;
}

Config::Options Config::DefaultOptions() {
  return {{kBlobfs, {}}, {kBootpart, {}}, {kFvm, {}},
          {kGpt, {}},    {kMinfs, {}},    {kFormatMinfsOnCorruption, {}}};
}

uint64_t Config::ReadUint64OptionValue(std::string_view key, uint64_t default_value) const {
  auto found = options_.find(key);
  if (found == options_.end())
    return default_value;

  uint64_t value = 0;
  if (sscanf(found->second.c_str(), "%" PRIu64, &value) != 1) {
    FX_LOGS(ERROR) << "Can't read integer option value for " << key << ", got " << found->second;
    return default_value;
  }

  return value;
}

std::ostream& operator<<(std::ostream& stream, const Config::Options& options) {
  bool first = true;
  for (const auto& option : options) {
    if (first) {
      first = false;
    } else {
      stream << ", ";
    }
    stream << option.first;
    if (!option.second.empty()) {
      stream << "=" << option.second;
    }
  }
  return stream;
}

}  // namespace devmgr
