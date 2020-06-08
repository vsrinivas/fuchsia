// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CMX_PROGRAM_H_
#define SRC_LIB_CMX_PROGRAM_H_

#include <string>
#include <string_view>
#include <vector>

#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {

// Class to parse the "program" attribute in a component manifest. This parses
// out any well-known attributes as well as preserving the original contents for
// forwarding to interested runners.
class ProgramMetadata {
 public:
  // Takes in a parsed value assumed to be corresponding to the "program"
  // attribute. Returns false if parsing failed.
  bool Parse(const rapidjson::Value& program_value, json::JSONParser* json_parser);

  bool IsBinaryNull() const { return binary_null_; }
  bool IsArgsNull() const { return args_null_; }
  bool IsEnvVarsNull() const { return env_vars_null_; }
  bool IsDataNull() const { return data_null_; }

  // Returns the "binary" attribute. Only applicable if this program is run as
  // an ELF binary or shell script.
  const std::string& binary() const { return binary_; }

  // Returns the "args" attribute.
  const std::vector<std::string>& args() const { return args_; }

  // Returns the "vars" attribute. Only applicable if this program is run as
  // an ELF binary or shell script.
  const std::vector<std::string>& env_vars() const { return env_vars_; }

  // Returns the "data" attribute. Applicable if this component is run with
  // non-ELF runner such as the Flutter or Dart runners. /pkg/data is a general
  // persistent storage.
  const std::string& data() const { return data_; }

  // Returns if the given attribute name is a well-known name. Runners are free to
  // define attributes outside the well-known set.
  bool IsWellKnownAttributeName(std::string_view name) const;

  using Attributes = std::vector<std::pair<std::string, std::string>>;
  const Attributes& unknown_attributes() const { return unknown_attributes_; }

 private:
  bool binary_null_ = true;
  bool args_null_ = true;
  bool env_vars_null_ = true;
  bool data_null_ = true;
  std::string binary_;
  std::vector<std::string> args_;
  std::vector<std::string> env_vars_;
  std::string data_;
  Attributes unknown_attributes_;

  bool ParseBinary(const rapidjson::Value& program_value, json::JSONParser* json_parser);
  bool ParseData(const rapidjson::Value& program_value, json::JSONParser* json_parser);
};

}  // namespace component

#endif  // SRC_LIB_CMX_PROGRAM_H_
