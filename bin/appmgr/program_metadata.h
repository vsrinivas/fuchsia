// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_PROGRAM_METADATA_H_
#define GARNET_BIN_APPMGR_PROGRAM_METADATA_H_

#include <string>
#include <vector>

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

// Class to parse the "program" attribute in a component manifest.
// TODO(geb): Use JSONParser to hold errors.
class ProgramMetadata {
 public:
  ProgramMetadata();
  ~ProgramMetadata();

  // Takes in a parsed value assumed to be corresponding to the "program"
  // attribute. Returns false if parsing failed.
  bool Parse(const rapidjson::Value& program_value);

  bool IsNull() const { return null_; }
  // Returns the "binary" attribute. Only applicable if this program is run as
  // an ELF binary.
  const std::string& binary() const { return binary_; }

 private:
  bool null_ = true;
  std::string binary_;
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_PROGRAM_METADATA_H_
