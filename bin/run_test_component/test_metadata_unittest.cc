// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/test_metadata.h"

#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/substitute.h"

namespace run {
namespace {

constexpr char kRequiredCmxElements[] = R"(
"program": {
  "binary": "path"
},
"sandbox": {
  "services": []
})";

class TestMetadataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE("", tmp_dir_.path()) << "Cannot acccess /tmp";
  }

  std::string NewFileFromString(const std::string& json) {
    std::string json_file;
    if (!tmp_dir_.NewTempFileWithData(json, &json_file)) {
      return "";
    }
    return json_file;
  }

  std::string CreateManifestJson(std::string additional_elements = "") {
    if (additional_elements == "") {
      return fxl::Substitute("{$0}", kRequiredCmxElements);
    } else {
      return fxl::Substitute("{$0, $1}", kRequiredCmxElements,
                             additional_elements);
    }
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(TestMetadataTest, InvalidJson) {
  const std::string json = R"JSON({
  "root": ["url1", "url3", "url5"]
  "sys": ["url2", "url4"]
  })JSON";
  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_TRUE(test_metadata.has_error());
  ASSERT_EQ(1u, test_metadata.errors().size());
}

TEST_F(TestMetadataTest, NoFacet) {
  const std::string json = CreateManifestJson();
  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_FALSE(test_metadata.has_error());
  ASSERT_EQ(0u, test_metadata.errors().size()) << test_metadata.errors()[0];
  ASSERT_TRUE(test_metadata.is_null());
}

TEST_F(TestMetadataTest, NoFuchsiaTestFacet) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
  })");
  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_FALSE(test_metadata.has_error());
  ASSERT_EQ(0u, test_metadata.errors().size()) << test_metadata.errors()[0];
  ASSERT_TRUE(test_metadata.is_null());
}

TEST_F(TestMetadataTest, NoServices) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
    }
  })");
  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_FALSE(test_metadata.has_error());
  ASSERT_EQ(0u, test_metadata.errors().size()) << test_metadata.errors()[0];
  ASSERT_FALSE(test_metadata.is_null());
  ASSERT_EQ(0u, test_metadata.services().size());
}

TEST_F(TestMetadataTest, InvalidTestFacet) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": [
    ]
  })");
  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_TRUE(test_metadata.has_error());
  ASSERT_EQ(1u, test_metadata.errors().size());
}

TEST_F(TestMetadataTest, InvalidServicesType) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": []
    }
  })");
  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_TRUE(test_metadata.has_error());
  ASSERT_EQ(1u, test_metadata.errors().size());
}

TEST_F(TestMetadataTest, InvalidServices) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        1: "url"
      }
    }
  })");
  {
    const std::string file = NewFileFromString(json);
    auto test_metadata = run::TestMetadata::CreateFromFile(file);
    EXPECT_TRUE(test_metadata.has_error());
    ASSERT_EQ(1u, test_metadata.errors().size());
  }

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "1": 2
      }
    }
  })");

  {
    const std::string file = NewFileFromString(json);
    auto test_metadata = run::TestMetadata::CreateFromFile(file);
    EXPECT_TRUE(test_metadata.has_error());
    ASSERT_EQ(1u, test_metadata.errors().size());
  }
}

std::pair<std::string, std::string> create_pair(const std::string& s1,
                                                const std::string& s2) {
  return std::make_pair(s1, s2);
}

TEST_F(TestMetadataTest, ValidServices) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "1": "url1",
        "2": "url2",
        "3": "url3"
      }
    }
  })");

  const std::string file = NewFileFromString(json);
  auto test_metadata = run::TestMetadata::CreateFromFile(file);
  EXPECT_FALSE(test_metadata.has_error());
  ASSERT_EQ(0u, test_metadata.errors().size());
  ASSERT_EQ(3u, test_metadata.services().size());
  EXPECT_THAT(
      test_metadata.services(),
      testing::ElementsAre(create_pair("1", "url1"), create_pair("2", "url2"),
                           create_pair("3", "url3")));
}

}  // namespace
}  // namespace run
