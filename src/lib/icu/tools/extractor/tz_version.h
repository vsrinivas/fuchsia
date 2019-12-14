// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by app BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ICU_TOOLS_EXTRACTOR_TZ_VERSION_H_
#define SRC_LIB_ICU_TOOLS_EXTRACTOR_TZ_VERSION_H_

#include <optional>
#include <string>

namespace icu_data_extractor {

// Command: "tz-version"
//
// Extracts the time zone version ID (e.g. "2019c") from the loaded ICU data and
// writes it to the output path, if given, or to STDOUT otherwise.
int ExtractTzVersion(std::optional<std::string> output_path);

}  // namespace icu_data_extractor

#endif  // SRC_LIB_ICU_TOOLS_EXTRACTOR_TZ_VERSION_H_
