// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/json/json_parser.h"

#include <stdlib.h>
#include <string>

#include "lib/fxl/files/file.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/error/en.h"

namespace json {
namespace {

using ErrorCallback = std::function<void(size_t, const std::string&)>;
using fxl::StringPrintf;

void GetLineAndColumnForOffset(const std::string& input, size_t offset,
                               int32_t* output_line, int32_t* output_column) {
  if (offset == 0) {
    // Errors at position 0 are assumed to be related to the whole file.
    *output_line = 0;
    *output_column = 0;
    return;
  }
  *output_line = 1;
  *output_column = 1;
  for (size_t i = 0; i < input.size() && i < offset; i++) {
    if (input[i] == '\n') {
      *output_line += 1;
      *output_column = 1;
    } else {
      *output_column += 1;
    }
  }
}
}  // namespace

rapidjson::Document JSONParser::ParseFromFile(const std::string& file) {
  file_ = file;
  std::string data;
  if (!files::ReadFileToString(file, &data)) {
    errors_.push_back(StringPrintf("Failed to read file: %s", file.c_str()));
    return rapidjson::Document();
  }
  return ParseFromString(data, file);
}

rapidjson::Document JSONParser::ParseFromString(const std::string& data,
                                                const std::string& file) {
  data_ = data;
  file_ = file;
  rapidjson::Document document;
  document.Parse(data_);
  if (document.HasParseError()) {
    ReportErrorInternal(document.GetErrorOffset(),
                        GetParseError_En(document.GetParseError()));
  }
  return document;
}

void JSONParser::ReportError(const std::string& error) {
  ReportErrorInternal(0, error);
}

void JSONParser::ReportErrorInternal(size_t offset, const std::string& error) {
  int32_t line;
  int32_t column;
  GetLineAndColumnForOffset(data_, offset, &line, &column);
  if (line == 0) {
    errors_.push_back(
        StringPrintf("%s: %s", file_.c_str(), error.c_str()));
  } else {
    errors_.push_back(StringPrintf("%s:%d:%d: %s", file_.c_str(),
                                   line, column, error.c_str()));
  }
}

bool JSONParser::HasError() const {
  return !errors_.empty();
}

std::string JSONParser::error_str() const {
  return fxl::JoinStrings(errors_, "\n");
}

}  // namespace json

