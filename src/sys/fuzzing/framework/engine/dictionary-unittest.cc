// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/dictionary.h"

#include <unordered_set>

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

// Test fixtures.

std::vector<std::string> Sort(const std::vector<std::string>& words) {
  std::vector<std::string> copy(words);
  std::sort(copy.begin(), copy.end());
  return copy;
}

std::vector<std::string> GetWords(const Dictionary& dict) {
  std::vector<std::string> words;
  dict.ForEachWord([&](const uint8_t* data, size_t size) {
    words.push_back(std::string(reinterpret_cast<const char*>(data), size));
  });
  return Sort(words);
}

std::vector<uint8_t> AsBytes(const std::string& str) {
  const auto* u8 = reinterpret_cast<const uint8_t*>(str.c_str());
  return std::vector<uint8_t>(u8, u8 + str.length());
}

// Unit tests.

TEST(DictionaryTest, DefaultConstructor) {
  Dictionary dict;
  dict.Configure(DefaultOptions());
  dict.ForEachWord([&](const uint8_t* data, size_t size) { FAIL(); });
}

TEST(DictionaryTest, Add) {
  // Data is chosen to have stricter constraints at lower levels.
  Dictionary dict;
  auto options = DefaultOptions();
  dict.Configure(options);

  std::vector<std::string> level0 = {
      "zero",
      "one",
      "two",
      "three",
  };
  dict.Add(AsBytes(level0[0]));
  dict.Add(AsBytes(level0[1]));
  dict.Add(AsBytes(level0[2]), 0);
  dict.Add(AsBytes(level0[3]), 0);

  std::vector<std::string> level1 = {
      "four",
      "five",
  };
  dict.Add(AsBytes(level1[0]), 1);
  dict.Add(AsBytes(level1[1]), 1);
  level1.insert(level1.end(), level0.begin(), level0.end());

  std::vector<std::string> level2 = {
      "six",
      "seven",
  };
  dict.Add(AsBytes(level2[0]), 2);
  dict.Add(AsBytes(level2[1]), 2);
  level2.insert(level2.end(), level1.begin(), level1.end());

  // Higher level includes any below.
  options->set_dictionary_level(0);
  auto words = GetWords(dict);
  EXPECT_EQ(words, Sort(level0));

  options->set_dictionary_level(1);
  words = GetWords(dict);
  EXPECT_EQ(words, Sort(level1));

  options->set_dictionary_level(3);
  words = GetWords(dict);
  EXPECT_EQ(words, Sort(level2));
}

TEST(DictionaryTest, ParseEmpty) {
  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_TRUE(dict.Parse(Input()));
  EXPECT_EQ(GetWords(dict).size(), 0U);
}

TEST(DictionaryTest, ParseBlank) {
  std::ostringstream oss;
  oss << std::endl;

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_TRUE(dict.Parse(Input(oss.str())));
  EXPECT_EQ(GetWords(dict).size(), 0U);
}

TEST(DictionaryTest, ParseBareWords) {
  std::ostringstream oss;
  oss << "bare_word";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseComment) {
  std::ostringstream oss;
  oss << "# comment";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_TRUE(dict.Parse(Input(oss.str())));
  EXPECT_EQ(GetWords(dict).size(), 0U);
}

TEST(DictionaryTest, ParseCommentWithSpaces) {
  std::ostringstream oss;
  oss << "    # comment with spaces";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_TRUE(dict.Parse(Input(oss.str())));
  EXPECT_EQ(GetWords(dict).size(), 0U);
}

TEST(DictionaryTest, ParseValidKeys) {
  std::ostringstream oss;
  oss << "key=\"valid\"" << std::endl;
  oss << "\"also valid\"" << std::endl;
  oss << "\"#valid\"" << std::endl;

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_TRUE(dict.Parse(Input(oss.str())));
  EXPECT_EQ(GetWords(dict), Sort({"valid", "also valid", "#valid"}));
}

TEST(DictionaryTest, ParseNoEquals) {
  std::ostringstream oss;
  oss << "missing \"=\"";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseInvalidKey) {
  std::ostringstream oss;
  oss << "key=\"invalid\"";
  oss << "invalid";
  oss << "\"\\\"#\"invalid";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseUnquoted) {
  std::ostringstream oss;
  oss << "unquoted_val=val";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseHalfQuoted) {
  std::ostringstream oss;
  oss << "halfquoted_val1=\"val" << std::endl;
  oss << "halfquoted_val2=val\"" << std::endl;

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseMissingValue) {
  std::ostringstream oss;
  oss << "missing_val=\"\"";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseMissingLevel) {
  std::ostringstream oss;
  oss << "missing_level@=\"val\"";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseInvalidLevel) {
  std::ostringstream oss;
  oss << "invalid_level@X=\"val\"";

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_FALSE(dict.Parse(Input(oss.str())));
}

TEST(DictionaryTest, ParseValidLevel) {
  std::ostringstream oss;
  oss << "valid_level@7=\"val1\"" << std::endl;
  oss << "valid_key=\"val2\"" << std::endl;

  Dictionary dict;
  auto options = DefaultOptions();
  dict.Configure(options);

  EXPECT_TRUE(dict.Parse(Input(oss.str())));
  EXPECT_EQ(GetWords(dict), Sort({"val2"}));

  options->set_dictionary_level(7);
  EXPECT_EQ(GetWords(dict), Sort({"val1", "val2"}));
}

TEST(DictionaryTest, ParseSpaces) {
  std::ostringstream oss;
  oss << "  spaces@0  =  \"  v  a  l  \"  " << std::endl;
  oss << "valid_key=\"val\"" << std::endl;

  Dictionary dict;
  dict.Configure(DefaultOptions());

  EXPECT_TRUE(dict.Parse(Input(oss.str())));
  EXPECT_EQ(GetWords(dict), Sort({"val", "  v  a  l  "}));
}

TEST(DictionaryTest, Parse) {
  std::ostringstream oss;
  oss << "  ####################  " << std::endl;
  oss << "  # complete example #  " << std::endl;
  oss << "  ####################  " << std::endl;

  oss << std::endl;
  oss << "# keys without level" << std::endl;
  oss << "  key_a = \"val a\" # a" << std::endl;
  oss << "  key_b = \"val b\" # b" << std::endl;
  oss << "  key_c = \"val c\" # c" << std::endl;

  oss << std::endl;
  oss << "# keys with explicit level=0" << std::endl;
  oss << "  key_0a@0 = \"val 0a\" # 0a" << std::endl;
  oss << "  key_0b@0 = \"val 0b\" # 0b" << std::endl;
  oss << "  key_0c@0 = \"val 0c\" # 0c" << std::endl;

  oss << std::endl;
  oss << "# keys with level=1" << std::endl;
  oss << "  key_1a@1 = \"val 1a\" # 1a" << std::endl;
  oss << "  key_1b@1 = \"val 1b\" # 1b" << std::endl;
  oss << "  key_1c@1 = \"val 1c\" # 1c" << std::endl;

  Dictionary dict;
  auto options = DefaultOptions();
  dict.Configure(options);

  EXPECT_TRUE(dict.Parse(Input(oss.str())));

  EXPECT_EQ(GetWords(dict), Sort({"val a", "val b", "val c", "val 0a", "val 0b", "val 0c"}));

  options->set_dictionary_level(1);
  EXPECT_EQ(GetWords(dict), Sort({"val a", "val b", "val c", "val 0a", "val 0b", "val 0c", "val 1a",
                                  "val 1b", "val 1c"}));
}

TEST(DictionaryTest, AsInput) {
  std::ostringstream oss;
  oss << "A=\"foo\"" << std::endl;
  oss << "B=\"\\\\bar\\\"\"" << std::endl;
  oss << "C@10=\"baz";

  // It's tricky to embed a null byte with ostringstream...
  auto str = oss.str();
  const auto* u8 = reinterpret_cast<const uint8_t*>(str.data());
  std::vector<uint8_t> bytes(u8, u8 + str.size());
  bytes.push_back(0x00);
  bytes.push_back(0xCA);
  bytes.push_back(0xFE);
  bytes.push_back(0x22);  // "
  bytes.push_back(0x0A);  // \n

  Dictionary dict;
  auto options = DefaultOptions();
  dict.Configure(options);

  EXPECT_TRUE(dict.Parse(Input(bytes)));
  options->set_dictionary_level(9);  // Should not affect output.
  auto input2 = dict.AsInput();

  // Convert to string to add a null terminator.
  std::string s(reinterpret_cast<const char*>(input2.data()), input2.size());
  EXPECT_STREQ(s.c_str(),
               "key1=\"foo\"\n"
               "key2=\"\\\\bar\\\"\"\n"
               "key3@10=\"baz\\x00\\xCA\\xFE\"\n");
}

}  // namespace
}  // namespace fuzzing
