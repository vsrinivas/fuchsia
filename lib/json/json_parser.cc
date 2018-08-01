// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/json/json_parser.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <functional>
#include <string>

#include <lib/fit/function.h>
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

rapidjson::Document JSONParser::ParseFromFileAt(int dirfd,
                                                const std::string& file) {
  file_ = file;
  std::string data;
  if (!files::ReadFileToStringAt(dirfd, file, &data)) {
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

void JSONParser::ParseFromDirectory(
    const std::string& path,
    fit::function<void(rapidjson::Document)> cb) {
  static constexpr char kPathTooLong[] =
      "Config directory path is too long: %s";
  char buf[PATH_MAX];
  buf[0] = '\0';
  if (strlcpy(buf, path.c_str(), PATH_MAX) >= PATH_MAX) {
    file_ = path;
    ReportError(fxl::StringPrintf(kPathTooLong, buf));
    return;
  }
  if (buf[strlen(buf) - 2] != '/' &&
      strlcat(buf, "/", PATH_MAX) >= PATH_MAX) {
    file_ = path;
    ReportError(fxl::StringPrintf(kPathTooLong, buf));
    return;
  }
  const size_t dir_len = strlen(buf);
  DIR* cfg_dir = opendir(path.c_str());
  if (cfg_dir != nullptr) {
    for (dirent* cfg = readdir(cfg_dir); cfg != nullptr;
         cfg = readdir(cfg_dir)) {
      if (strcmp(".", cfg->d_name) == 0 || strcmp("..", cfg->d_name) == 0) {
        continue;
      }
      if (strlcat(buf, cfg->d_name, PATH_MAX) >= PATH_MAX) {
        file_ = path;
        ReportError(fxl::StringPrintf(kPathTooLong, buf));
        continue;
      }
      rapidjson::Document document = ParseFromFile(buf);
      if (!document.HasParseError() && !document.IsNull()) {
        cb(std::move(document));
      }
      // Reset buf to directory path.
      buf[dir_len] = '\0';
    }
    closedir(cfg_dir);
  } else {
    file_ = path;
    ReportError("Could not open config directory.");
  }
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

