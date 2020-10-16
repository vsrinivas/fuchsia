// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build_info.h"

#include "src/lib/files/file.h"

namespace harvester {

BuildInfoValue ManifestFinder::Find() {
  if (content_.empty()) {
    return BuildInfoValue(BuildInfoError::kEmptyFile);
  }
  // Find the one "project" with @name=name_.
  if (!MoveToProjectWithName(name_)) {
    return BuildInfoValue(BuildInfoError::kMissingProject);
  }
  // We are pointing at the project now so return the value of attribute_.
  return GetAttributeForCurrentElement(attribute_);
}

bool ManifestFinder::MovePast(char c) {
  if (pos_ >= content_.length()) {
    return false;
  }
  pos_ = content_.find_first_of(c, pos_);
  if (pos_ == std::string::npos) {
    return false;
  }
  ++pos_;
  return pos_ < content_.length();
}

bool ManifestFinder::MoveTo(const std::string& target) {
  pos_ = content_.find(target, pos_);
  return pos_ != std::string::npos;
}

bool ManifestFinder::MoveToProjectWithName(const std::string& name) {
  std::string project = "<project ";
  std::string name_attr = "name=";

  while (pos_ < content_.length()) {
    // Move to the next project or return false if finding one fails.
    if (!MoveTo(project)) {
      return false;
    }

    size_t name_pos = content_.find(name_attr, pos_);
    if (name_pos == std::string::npos) {
      return false;
    }

    // The name attribute looks like name="xxx", this moves name_pos to point
    // at the first " after the =.
    name_pos += name_attr.length();
    if (name_pos >= content_.length()) {
      return false;
    }

    std::string target = '"' + name + '"';

    // If the name matches exactly then pos_ is pointing at the beginning of
    // this project element so we can just return true.
    if (content_.compare(name_pos, target.length(), target) == 0) {
      return true;
    }
    // Otherwise we move pos_ past this project and loop back around to look
    // at the next one if possible.
    if (!MovePast('>')) {
      return false;
    }
  }

  return false;
}

BuildInfoValue ManifestFinder::GetAttributeForCurrentElement(
    const std::string& attr) {
  size_t attr_pos = content_.find(attr + '=', pos_);
  if (attr_pos == std::string::npos) {
    return BuildInfoValue(BuildInfoError::kMissingAttribute);
  }
  size_t attr_value_begin = attr_pos + attr.length() + 2;
  if (attr_value_begin >= content_.length()) {
    return BuildInfoValue(BuildInfoError::kMalformedFile);
  }

  size_t attr_value_end = content_.find_first_of('"', attr_value_begin);
  if (attr_value_end == std::string::npos ||
      attr_value_begin >= attr_value_end) {
    return BuildInfoValue(BuildInfoError::kMalformedFile);
  }

  size_t check_format = content_.find_first_of(">=", attr_value_begin);
  if (check_format == std::string::npos || check_format < attr_value_end) {
    return BuildInfoValue(BuildInfoError::kMalformedFile);
  }

  return BuildInfoValue(
      content_.substr(attr_value_begin, attr_value_end - attr_value_begin));
}

// Reads /config/build-info/snapshot and returns the value of
// manifest/projects/project[@name="fuchsia"]/@revision
// Returns an empty string if this fails for any reason.
BuildInfoValue GetFuchsiaBuildVersion() {
  std::string filepath("/config/build-info/snapshot");
  std::string content;
  if (!files::ReadFileToString(filepath, &content)) {
    return BuildInfoValue(BuildInfoError::kMissingFile);
  }

  if (content.empty()) {
    return BuildInfoValue(BuildInfoError::kEmptyFile);
  }

  return ManifestFinder(content, "fuchsia", "revision").Find();
}

}  // namespace harvester
