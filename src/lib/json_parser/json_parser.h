// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_JSON_PARSER_JSON_PARSER_H_
#define SRC_LIB_JSON_PARSER_JSON_PARSER_H_

#include <stdlib.h>

#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "lib/fit/function.h"
#include "src/lib/fxl/macros.h"

namespace json_parser {

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
  rapidjson::Document ParseFromString(const std::string& data, const std::string& file);

  // Initialize multiple documents from files in a directory. |cb| is
  // called for each file that parses. The traversal is not recursive, and all
  // files in the directory are expected to be JSON files. If the directory does
  // not exist, no error is reported, and no callbacks are called. Callers
  // wishing to identify such a state should stat the path themselves.
  //
  // It is up to the caller to decide how to merge multiple documents.
  void ParseFromDirectory(const std::string& path, fit::function<void(rapidjson::Document)> cb);

  // Like |ParseFromDirectory|, but relative to a directory.
  void ParseFromDirectoryAt(int dirfd, const std::string& path,
                            fit::function<void(rapidjson::Document)> cb);

  // Copies the string values from a |name|d |value| to the |out| vector.
  // Clears |out| and calls ReportError() if |value| does not refer to an array,
  // or if the array contains non-string values.
  void CopyStringArray(const std::string& name, const rapidjson::Value& value,
                       std::vector<std::string>* out);

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

}  // namespace json_parser

namespace json {
// The JSONParser should be used from the json_parser namespace to be consistent with the rest of
// this library but currently many callsites expect this to live in the json namespace. This alias
// allows both names to work.
// TODO(fxbug.dev/36759): Update callers to new name and remove this alias.
using JSONParser = ::json_parser::JSONParser;
}  // namespace json

#endif  // SRC_LIB_JSON_PARSER_JSON_PARSER_H_
