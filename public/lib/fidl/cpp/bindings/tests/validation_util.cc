// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/tests/validation_util.h"

#include <stdio.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/tests/validation_test_input_parser.h"
#include "mojo/public/cpp/test_support/test_support.h"

namespace fidl {
namespace test {
namespace validation_util {

std::string GetValidationDataPath(const std::string& root,
                                  const std::string& suffix) {
  return "lib/fidl/compiler/interfaces/tests/data/validation/" + root +
         suffix;
}

bool ReadFile(const std::string& path, std::string* result) {
  FILE* fp = OpenSourceRootRelativeFile(path.c_str());
  if (!fp) {
    ADD_FAILURE() << "File not found: " << path;
    return false;
  }
  fseek(fp, 0, SEEK_END);
  size_t size = static_cast<size_t>(ftell(fp));
  if (size == 0) {
    result->clear();
    fclose(fp);
    return true;
  }
  fseek(fp, 0, SEEK_SET);
  result->resize(size);
  size_t size_read = fread(&result->at(0), 1, size, fp);
  fclose(fp);
  return size == size_read;
}

bool ReadAndParseDataFile(const std::string& path,
                          std::vector<uint8_t>* data,
                          size_t* num_handles) {
  std::string input;
  if (!ReadFile(path, &input))
    return false;

  std::string error_message;
  if (!ParseValidationTestInput(input, data, num_handles, &error_message)) {
    ADD_FAILURE() << error_message;
    return false;
  }

  return true;
}

bool ReadResultFile(const std::string& path, std::string* result) {
  if (!ReadFile(path, result))
    return false;

  // Result files are new-line delimited text files. Remove any CRs.
  result->erase(std::remove(result->begin(), result->end(), '\r'),
                result->end());

  // Remove trailing LFs.
  size_t pos = result->find_last_not_of('\n');
  if (pos == std::string::npos)
    result->clear();
  else
    result->resize(pos + 1);

  return true;
}

bool ReadTestCase(const std::string& test_name,
                  std::vector<uint8_t>* data,
                  size_t* num_handles,
                  std::string* expected) {
  if (!ReadAndParseDataFile(GetValidationDataPath(test_name, ".data"), data,
                            num_handles) ||
      !ReadResultFile(GetValidationDataPath(test_name, ".expected"),
                      expected)) {
    return false;
  }

  return true;
}

std::vector<std::string> GetMatchingTests(const std::string& prefix) {
  std::vector<std::string> names = EnumerateSourceRootRelativeDirectory(
      validation_util::GetValidationDataPath("", ""));
  const std::string suffix = ".data";
  std::vector<std::string> tests;
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i].size() >= suffix.size() &&
        names[i].substr(0, prefix.size()) == prefix &&
        names[i].substr(names[i].size() - suffix.size()) == suffix)
      tests.push_back(names[i].substr(0, names[i].size() - suffix.size()));
  }
  return tests;
}

}  // namespace validation_util
}  // namespace test
}  // namespace fidl
