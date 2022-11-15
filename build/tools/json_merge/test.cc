// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <gtest/gtest.h>

#include "build/tools/json_merge/json_merge.h"

namespace {

class JsonMerge : public ::testing::Test {
 protected:
  void AddInput(const std::string& filename, const std::string& input) {
    inputs.push_back({.name = filename, .contents = std::make_unique<std::istringstream>(input)});
  }

  int Merge(const MergeConfig& config = {}) { return JSONMerge(inputs, output, errors, config); }

  std::string Output() { return output.str(); }

  std::string Errors() { return errors.str(); }

  void ExpectNoErrors() { EXPECT_TRUE(Errors().empty()); }

  void ExpectError(const std::string& expected_error) { EXPECT_EQ(Errors(), expected_error); }

 private:
  std::vector<input_file> inputs;
  std::ostringstream output;
  std::ostringstream errors;
};

TEST_F(JsonMerge, MergeOne) {
  const std::string input = R"JSON({
    "key1": {
        "key2": [
            "value1",
            "value2",
            "value3"
        ],
        "key3": "value4"
    }
})JSON";
  AddInput("file1.json", input);

  EXPECT_EQ(Merge(), 0);
  EXPECT_EQ(Output(), input);
  ExpectNoErrors();
}

TEST_F(JsonMerge, MergeOneAndMinify) {
  const std::string input = R"JSON({
    "key1": {
        "key2": [
            "value1",
            "value2",
            "value3"
        ],
        "key3": "value4"
    }
})JSON";
  AddInput("file1.json", input);

  EXPECT_EQ(Merge({.minify = true}), 0);
  const std::string output =
      R"JSON({"key1":{"key2":["value1","value2","value3"],"key3":"value4"}})JSON";
  EXPECT_EQ(Output(), output);
  ExpectNoErrors();
}

TEST_F(JsonMerge, MergeThree) {
  const std::string input1 = R"JSON({
    "key1": "value1"
})JSON";
  AddInput("file1.json", input1);
  const std::string input2 = R"JSON({
    "key2": "value2"
})JSON";
  AddInput("file2.json", input2);
  const std::string input3 = R"JSON({
    "key3": "value3"
})JSON";
  AddInput("file3.json", input3);

  EXPECT_EQ(Merge(), 0);
  const std::string output = R"JSON({
    "key1": "value1",
    "key2": "value2",
    "key3": "value3"
})JSON";
  EXPECT_EQ(Output(), output);
  ExpectNoErrors();
}

TEST_F(JsonMerge, MergeConflict) {
  const std::string input1 = R"JSON({
    "key1": "value1"
})JSON";
  AddInput("file1.json", input1);
  const std::string input2 = R"JSON({
    "key1": "value2"
})JSON";
  AddInput("file2.json", input2);

  EXPECT_NE(Merge(), 0);
  ExpectError("file2.json has a conflicting value for key \"key1\"!\n");
}

TEST_F(JsonMerge, DeepMerge) {
  const std::string input1 = R"JSON({"key1": { "subkey1": 1 }})JSON";
  AddInput("file1.json", input1);
  const std::string input2 = R"JSON({"key1": { "subkey2": 2 }})JSON";
  AddInput("file2.json", input2);

  EXPECT_EQ(Merge({.deep_merge = true, .minify = true}), 0);
  EXPECT_EQ(Output(), R"JSON({"key1":{"subkey1":1,"subkey2":2}})JSON");
  ExpectNoErrors();
}

TEST_F(JsonMerge, DefaultNoTrailingComma) {
  const std::string input1 = R"JSON({"key1":"value",})JSON";
  AddInput("file1.json", input1);

  // Should fail to parse the input with a trailing comma.
  EXPECT_NE(Merge(), 0);
}

TEST_F(JsonMerge, DefaultNoComments) {
  const std::string input1 = R"JSON({"key1":"value" /*comment*/})JSON";
  AddInput("file1.json", input1);

  // Should fail to parse the input with a comment.
  EXPECT_NE(Merge(), 0);
}

TEST_F(JsonMerge, Relaxed) {
  const std::string input1 = R"JSON({
    "key1":"value",
    "key2":{/*comment*/}, // <-traling comma
})JSON";
  AddInput("file1.json", input1);

  EXPECT_EQ(Merge({.relaxed_input = true}), 0) << Errors();
  EXPECT_EQ(Output(), R"JSON({
    "key1": "value",
    "key2": {}
})JSON");
  ExpectNoErrors();
}

TEST_F(JsonMerge, RelaxedMinify) {
  const std::string input1 = R"JSON({"key1":"value",/*comment*/})JSON";
  AddInput("file1.json", input1);

  EXPECT_EQ(Merge({.relaxed_input = true, .minify = true}), 0);
  EXPECT_EQ(Output(), R"JSON({"key1":"value"})JSON");
  ExpectNoErrors();
}

}  // namespace
