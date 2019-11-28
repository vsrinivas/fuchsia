// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/json_parser/json_parser.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include <functional>
#include <string>

#include <fbl/unique_fd.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "lib/fit/function.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace json_parser {
namespace {

using ErrorCallback = fit::function<void(size_t, const std::string&)>;
using fxl::StringPrintf;

void GetLineAndColumnForOffset(const std::string& input, size_t offset, int32_t* output_line,
                               int32_t* output_column) {
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

rapidjson::Document JSONParser::ParseFromFileAt(int dirfd, const std::string& file) {
  file_ = file;
  std::string data;
  if (!files::ReadFileToStringAt(dirfd, file, &data)) {
    errors_.push_back(StringPrintf("Failed to read file: %s", file.c_str()));
    return rapidjson::Document();
  }
  return ParseFromString(data, file);
}

rapidjson::Document JSONParser::ParseFromString(const std::string& data, const std::string& file) {
  data_ = data;
  file_ = file;
  rapidjson::Document document;
  document.Parse(data_);
  if (document.HasParseError()) {
    ReportErrorInternal(document.GetErrorOffset(), GetParseError_En(document.GetParseError()));
  }
  return document;
}

void JSONParser::ParseFromDirectory(const std::string& path,
                                    fit::function<void(rapidjson::Document)> cb) {
  ParseFromDirectoryAt(AT_FDCWD, path, std::move(cb));
}

void JSONParser::ParseFromDirectoryAt(int dirfd, const std::string& path,
                                      fit::function<void(rapidjson::Document)> cb) {
  fbl::unique_fd dir_fd(openat(dirfd, path.c_str(), O_RDONLY | O_DIRECTORY));
  if (!dir_fd.is_valid()) {
    ReportError(
        fxl::StringPrintf("Could not open directory %s error %s", path.c_str(), strerror(errno)));
    return;
  }

  std::vector<std::string> dir_entries;
  if (!files::ReadDirContentsAt(dir_fd.get(), ".", &dir_entries)) {
    ReportError(fxl::StringPrintf("Could not read directory contents from path %s error %s",
                                  path.c_str(), strerror(errno)));
    return;
  }
  for (const auto& entry : dir_entries) {
    if (!files::IsFileAt(dir_fd.get(), entry))
      continue;

    rapidjson::Document document = ParseFromFileAt(dir_fd.get(), entry);
    if (!document.HasParseError() && !document.IsNull()) {
      cb(std::move(document));
    }
  }
}

void JSONParser::CopyStringArray(const std::string& name, const rapidjson::Value& value,
                                 std::vector<std::string>* out) {
  out->clear();
  if (!value.IsArray()) {
    ReportError(fxl::StringPrintf("'%s' is not an array.", name.c_str()));
    return;
  }
  for (const auto& entry : value.GetArray()) {
    if (!entry.IsString()) {
      ReportError(fxl::StringPrintf("'%s' contains an item that's not a string", name.c_str()));
      out->clear();
      return;
    }
    out->push_back(entry.GetString());
  }
}

void JSONParser::ReportError(const std::string& error) { ReportErrorInternal(0, error); }

void JSONParser::ReportErrorInternal(size_t offset, const std::string& error) {
  int32_t line;
  int32_t column;
  GetLineAndColumnForOffset(data_, offset, &line, &column);
  if (line == 0) {
    errors_.push_back(StringPrintf("%s: %s", file_.c_str(), error.c_str()));
  } else {
    errors_.push_back(StringPrintf("%s:%d:%d: %s", file_.c_str(), line, column, error.c_str()));
  }
}

bool JSONParser::HasError() const { return !errors_.empty(); }

std::string JSONParser::error_str() const { return fxl::JoinStrings(errors_, "\n"); }

}  // namespace json_parser
