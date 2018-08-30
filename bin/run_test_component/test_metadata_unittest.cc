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

  void ExpectFailedParse(const std::string& json,
                         const std::string& expected_error) {
    std::string error;
    TestMetadata tm;
    EXPECT_FALSE(ParseFrom(&tm, json));
    EXPECT_TRUE(tm.HasError());
    EXPECT_THAT(tm.error_str(), ::testing::HasSubstr(expected_error));
  }

  bool ParseFrom(TestMetadata* tm, const std::string& json) {
    std::string json_path;
    if (!tmp_dir_.NewTempFileWithData(json, &json_path)) {
      return false;
    }
    const bool ret = tm->ParseFromFile(json_path);
    EXPECT_EQ(ret, !tm->HasError());
    return ret;
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
  const std::string json = R"JSON({,,,})JSON";
  ExpectFailedParse(json, "Missing a name for object member.");
}

TEST_F(TestMetadataTest, NoFacet) {
  const std::string json = CreateManifestJson();
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_TRUE(tm.is_null());
}

TEST_F(TestMetadataTest, NoFuchsiaTestFacet) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
  })");
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_TRUE(tm.is_null());
}

TEST_F(TestMetadataTest, NoServices) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
    }
  })");
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_FALSE(tm.is_null());
  EXPECT_THAT(tm.services(), ::testing::IsEmpty());
}

TEST_F(TestMetadataTest, InvalidTestFacet) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": [
    ]
  })");
  ExpectFailedParse(json, "'fuchsia.test' in 'facets' should be an object.");
}

TEST_F(TestMetadataTest, InvalidServicesType) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": []
    }
  })");
  ExpectFailedParse(json,
                    "'injected-services' in 'fuchsia.test' should be an "
                    "object.");
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
  ExpectFailedParse(json, "Missing a name for object member.");

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "1": 2
      }
    }
  })");
  ExpectFailedParse(json,
                    "'injected-services' in 'fuchsia.test' has a "
                    "non-string pair.");
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

  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_THAT(
      tm.services(),
      testing::ElementsAre(create_pair("1", "url1"), create_pair("2", "url2"),
                           create_pair("3", "url3")));
}

}  // namespace
}  // namespace run
