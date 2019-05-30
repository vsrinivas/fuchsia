// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_BUGREPORT_BUG_REPORT_CLIENT_H_
#define SRC_DEVELOPER_BUGREPORT_BUG_REPORT_CLIENT_H_

#include <fstream>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bugreport {

// Meant to represent a single unit of data gathered from the input json
// document. Each one of these should normally be outputted to its own file.
struct Target {
  std::string name;
  std::string contents;
};

// Complete stage of processing: parsing, validating and separating.
std::optional<std::vector<Target>> ProcessBugReport(
    const std::string& json_input);

// |output_path| is the directory where the targets will be generated. It will
// create it if it doesn't exist.
// Returns false if the path is invalid (and could not be created) or if one or
// more of the targets could not be exported.
bool Export(const std::vector<Target>& targets,
            const std::filesystem::path& output_path);

// Convenience over ProcessBugReport and Export.
// Returns the list of generated targets if successful.
std::optional<std::vector<Target>> HandleBugReport(
    const std::filesystem::path& output_path, std::istream* input);

}  // namespace bugreport

#endif  // SRC_DEVELOPER_BUGREPORT_BUG_REPORT_CLIENT_H_
