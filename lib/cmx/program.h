// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CMX_PROGRAM_H_
#define GARNET_LIB_CMX_PROGRAM_H_

#include <string>
#include <vector>

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

// Class to parse the "program" attribute in a component manifest.
class ProgramMetadata {
 public:
  // Takes in a parsed value assumed to be corresponding to the "program"
  // attribute. Returns false if parsing failed.
  bool Parse(const rapidjson::Value& program_value,
             json::JSONParser* json_parser);

  bool IsNull() const { return null_; }
  // Returns the "binary" attribute. Only applicable if this program is run as
  // an ELF binary.
  const std::string& binary() const { return binary_; }

 private:
  bool null_ = true;
  std::string binary_;
};

}  // namespace component

#endif  // GARNET_LIB_CMX_PROGRAM_H_
