// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build_info.h"

#include <gtest/gtest.h>

class BuildInfoTest : public ::testing::Test {};

TEST_F(BuildInfoTest, FuchsiaBuildVersion) {
  harvester::BuildInfoValue version = harvester::GetFuchsiaBuildVersion();

  EXPECT_TRUE(version.HasValue());

  // This is an example expected value: 9baab964aee53585a71df9f087e667b12addfa10
  std::string value = version.Value();
  EXPECT_EQ(value.length(), 40UL);
  EXPECT_TRUE(std::all_of(value.begin(), value.end(),
                          [](unsigned char c) { return std::isxdigit(c); }));
}

class ManifestFinderTest : public ::testing::Test {};

TEST_F(ManifestFinderTest, NoContent) {
  std::string content = "";
  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kEmptyFile);
}

TEST_F(ManifestFinderTest, ValidContent) {
  std::string hash = "9baab964aee53585a71df9f087e667b12addfa10";
  std::string content = R"END(
<manifest>
  <project name="fizz" foobar="hello">
    <item name="buzz"/>
  </project>
  <project name="fuchsia" foobar="hello" revision="9baab964aee53585a71df9f087e667b12addfa10"
    something="elese">
    <item name="stuff"/>
  </project>
</manifest>
)END";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasValue());
  EXPECT_EQ(result.Value(), hash);
}

TEST_F(ManifestFinderTest, MalformedFileNoString) {
  std::string content = R"END(
<manifest>
  <project name="fizz" foobar="hello">
    <item name="buzz"/>
  </project>
  <project name="fuchsia" foobar="hello" revision=)END";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kMalformedFile);
}

TEST_F(ManifestFinderTest, MalformedFileEarlyEOF) {
  std::string content = R"END(
<manifest>
  <project name="fizz" foobar="hello">
    <item name="buzz"/>
  </project>
  <project name="fuchsia" foobar="hello" revision="12345)END";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kMalformedFile);
}

TEST_F(ManifestFinderTest, MalformedFileMissingQuotes) {
  std::string content = R"END(
<manifest>
  <project name="fizz" foobar="hello">
    <item name="buzz"/>
  </project>
  <project name="fuchsia" foobar="hello" revision=12345 other="foo">
    <item name="buzz"/>
  </project>
</manifest>
)END";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kMalformedFile);
}

TEST_F(ManifestFinderTest, MissingAttribute) {
  std::string content = R"END(
<manifest>
  <project name="fizz" foobar="hello">
    <item name="buzz"/>
  </project>
  <project name="fuchsia" foobar="hello">
    <item name="hello"/>
  </project>
  <project name="hello" foobar="hello">
    <item name="goodbye"/>
  </project>
</manifest>
)END";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kMissingAttribute);
}

TEST_F(ManifestFinderTest, MissingProjectNoProjects) {
  std::string content = "<foobar></foobar>";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kMissingProject);
}

TEST_F(ManifestFinderTest, MissingProjectNoProjectWithRightName) {
  std::string content = R"END(
<manifest>
  <project name="foobar" revision="123">
    <item name="bar"/>
  </project>
  <project name="fizzbuzz" revision="456">
    <item name="foo"/>
  </project>
</manifest>
)END";

  harvester::ManifestFinder finder =
      harvester::ManifestFinder(content, "fuchsia", "revision");

  harvester::BuildInfoValue result = finder.Find();

  EXPECT_TRUE(result.HasError());
  EXPECT_EQ(result.Error(), harvester::BuildInfoError::kMissingProject);
}
