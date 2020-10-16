// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_BUILD_INFO_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_BUILD_INFO_H_

#include <lib/syslog/cpp/macros.h>

#include <optional>

namespace harvester {

enum class BuildInfoError {
  kEmptyFile,
  kMalformedFile,
  kMissingAttribute,
  kMissingFile,
  kMissingProject,
};

inline std::string ToString(BuildInfoError error) {
  switch (error) {
    case BuildInfoError::kEmptyFile:
      return "BuildInfoError::kEmptyFile";
    case BuildInfoError::kMalformedFile:
      return "BuildInfoError::kMalformedFile";
    case BuildInfoError::kMissingAttribute:
      return "BuildInfoError::kMissingAttribute";
    case BuildInfoError::kMissingProject:
      return "BuildInfoError::kMissingProject";
    case BuildInfoError::kMissingFile:
      return "BuildInfoError::kMissingFile";
  }
}

class BuildInfoValue {
 public:
  explicit BuildInfoValue(const std::string& value)
      : value_(value), error_(std::nullopt) {}
  explicit BuildInfoValue(enum BuildInfoError error)
      : value_(std::nullopt), error_(error) {}

  bool HasValue() const { return value_.has_value(); }

  const std::string& Value() const {
    FX_CHECK(HasValue());
    return value_.value();
  }

  bool HasError() const { return error_.has_value(); }

  enum BuildInfoError Error() const {
    FX_CHECK(HasError());
    return error_.value();
  }

  bool operator==(const BuildInfoValue& other) const {
    return (value_ == other.value_) && (error_ == other.error_);
  }

 private:
  std::optional<std::string> value_;
  std::optional<enum BuildInfoError> error_;
};

class ManifestFinder {
 public:
  ManifestFinder(std::string content, std::string name, std::string attribute)
      : content_(std::move(content)),
        name_(std::move(name)),
        attribute_(std::move(attribute)),
        pos_(0) {}

  // Finds the value of |attribute| in the first element of type "project" with
  // name matching |name| inside the XML document |content|.
  BuildInfoValue Find();

 private:
  // Finds the next instance of c after pos_ and then moves one position past
  // that.
  // Returns false if this fails for any reason.
  bool MovePast(char c);

  // Moves the current position to the beginning of the next instance of target.
  // Returns false if target does not exist.
  bool MoveTo(const std::string& target);

  // Moves the current position to the beginning of a <project> element with
  // name attribute equal to the input name parameter.
  // Returns false if this fails for any reason.
  bool MoveToProjectWithName(const std::string& name);

  // Gets the string value associated with attr inside the current element.
  // This assumes that pos_ is pointing at the start of a <project> element.
  // This assumes the value of attr is a double quoted string.
  BuildInfoValue GetAttributeForCurrentElement(const std::string& attr);

  std::string content_;
  std::string name_;
  std::string attribute_;
  size_t pos_;
};

// Reads /config/build-info/snapshot and returns the value of
// manifest/projects/project[@name="fuchsia"]/@revision
// Returns an error upon failure.
BuildInfoValue GetFuchsiaBuildVersion();

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_BUILD_INFO_H_
