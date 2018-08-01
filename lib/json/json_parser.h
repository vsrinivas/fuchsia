// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_JSON_JSON_PARSER_H_
#define GARNET_LIB_JSON_JSON_PARSER_H_

#include <stdlib.h>
#include <string>
#include <vector>

#include <lib/fit/function.h>
#include "lib/fxl/macros.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace json {

// A JSON parser utility class.
//
// This class provides general facilities to parse a JSON file, and report
// errors. If parsing succeeds, ReadFrom() returns a rapidjson::Document
// representing the file. The class is agnostic to the actual structure of
// the document -- client code is responsible for interpreting the
// rapidjson::Document, which could contain any valid JSON.
class JSONParser {
 public:
  JSONParser() = default;
  JSONParser(JSONParser&&) = default;
  JSONParser& operator=(JSONParser&&) = default;

  // Parses a JSON file. If reading or parsing the file fails, reports errors in
  // error_str(). May be called multiple times, for example on multiple files,
  // in which case any previous errors will be retained.
  rapidjson::Document ParseFromFile(const std::string& file);

  // Like |ParseFromFile|, but relative to a directory.
  rapidjson::Document ParseFromFileAt(int dirfd, const std::string& file);

  // Initialize the document from a JSON string |data|. If parsing fails,
  // reports errors in error_str(). |file| is not read, but it is used as the
  // prefix for lines in error_str(). May be called multiple times, for example
  // on multiple files, in which case any previous errors will be retained.
  rapidjson::Document ParseFromString(const std::string& data,
                                      const std::string& file);

  // Initialize multiple documents from files in a directory. |cb| is
  // called for each file that parses. The traversal is not recursive, and all
  // files in the directory are expected to be JSON files.
  //
  // It is up to the caller to decide how to merge multiple documents.
  void ParseFromDirectory(const std::string& path,
                          fit::function<void(rapidjson::Document)> cb);

  // Returns true if there was an error initializing the document.
  bool HasError() const;
  // If HasError() is true, returns a human-readable string describing the
  // error(s) initializing the document.
  std::string error_str() const;

  // Records an error initializing the object. Multiple errors may be recorded.
  void ReportError(const std::string& error);

 private:
  // Internal version of |ReportError| that includes an offset.
  void ReportErrorInternal(size_t offset, const std::string& error);

  std::vector<std::string> errors_;
  // Stores the filename, for reporting debug information.
  std::string file_;
  // Stores the file content, for reporting debug information.
  std::string data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JSONParser);
};

}  // namespace json

#endif  // GARNET_LIB_JSON_JSON_PARSER_H_
