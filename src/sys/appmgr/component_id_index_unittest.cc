// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/component_id_index.h"

#include <fcntl.h>
#include <zircon/assert.h>

#include <gtest/gtest.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/files/unique_fd.h"
#include "src/sys/appmgr/moniker.h"

namespace component {
namespace {

constexpr char kIndexFilePath[] = "component_id_index";

class ComponentIdIndexTest : public ::testing::Test {
 protected:
  fxl::UniqueFD MakeAppmgrConfigDir() {
    fxl::UniqueFD ufd(open(tmp_dir_.path().c_str(), O_RDONLY));
    ZX_ASSERT(ufd.is_valid());
    return ufd;
  }

  fxl::UniqueFD MakeAppmgrConfigDirWithIndex(std::string json_index) {
    auto dirfd = MakeAppmgrConfigDir();
    ZX_ASSERT(
        files::WriteFileAt(dirfd.get(), kIndexFilePath, json_index.data(), json_index.size()));
    return dirfd;
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

// Test that it's OK if index file doesn't exist; it is optional.
// An empty component ID Index is produced instead.
TEST_F(ComponentIdIndexTest, MissingConfigFile) {
  auto result = ComponentIdIndex::CreateFromAppmgrConfigDir(MakeAppmgrConfigDir());
  EXPECT_TRUE(result.is_ok());
}

// Index file should be valid JSON.
TEST_F(ComponentIdIndexTest, InvalidJsonConfig) {
  auto config_dir = MakeAppmgrConfigDirWithIndex("invalid index contents");
  auto result = ComponentIdIndex::CreateFromAppmgrConfigDir(std::move(config_dir));
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(ComponentIdIndex::Error::INVALID_JSON, result.error());
}

TEST_F(ComponentIdIndexTest, LookupInstanceId_Exists) {
  auto config_dir = MakeAppmgrConfigDirWithIndex(R"({
    "instances": [
      {
        "instance_id": "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280",
        "appmgr_moniker": {
          "realm_path": ["sys"],
          "url": "fuchsia-pkg://example.com/pkg#meta/component.cmx"
        }
      }
    ]
  })");
  auto result = ComponentIdIndex::CreateFromAppmgrConfigDir(std::move(config_dir));
  EXPECT_FALSE(result.is_error());

  auto index = result.take_value();
  Moniker moniker = {.url = "fuchsia-pkg://example.com/pkg#meta/component.cmx",
                     .realm_path = {"sys"}};
  auto id = index->LookupMoniker(moniker).value_or("");
  EXPECT_EQ("8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280", id);
}

TEST_F(ComponentIdIndexTest, LookupMonikerNotExists) {
  // the instance_id below is 63 hexchars (252 bits) instead of 64 hexchars (256 bits)
  auto config_dir = MakeAppmgrConfigDirWithIndex(R"({"instances" : []})");
  auto result = ComponentIdIndex::CreateFromAppmgrConfigDir(std::move(config_dir));
  EXPECT_FALSE(result.is_error());
  auto index = result.take_value();

  Moniker moniker = {.url = "fuchsia-pkg://example.com/pkg#meta/component.cmx",
                     .realm_path = {"sys"}};
  EXPECT_FALSE(index->LookupMoniker(moniker).has_value());
}

TEST_F(ComponentIdIndexTest, ShouldNotRestrictIsolatedPersistentStorage) {
  auto config_dir = MakeAppmgrConfigDirWithIndex(R"({"instances" : []})");
  auto result = ComponentIdIndex::CreateFromAppmgrConfigDir(std::move(config_dir));
  EXPECT_FALSE(result.is_error());
  auto index = result.take_value();
  EXPECT_FALSE(index->restrict_isolated_persistent_storage());
}

TEST_F(ComponentIdIndexTest, ShouldRestrictIsolatedPersistentStorage) {
  auto config_dir = MakeAppmgrConfigDirWithIndex(
      R"({"appmgr_restrict_isolated_persistent_storage": true, "instances" : []})");
  auto result = ComponentIdIndex::CreateFromAppmgrConfigDir(std::move(config_dir));
  EXPECT_FALSE(result.is_error());
  auto index = result.take_value();
  EXPECT_TRUE(index->restrict_isolated_persistent_storage());
}

TEST_F(ComponentIdIndexTest, ParseErrors) {
  struct TestCase {
    std::string name;
    std::string index;
    ComponentIdIndex::Error expected;
  };

  std::vector<TestCase> test_cases = {
      TestCase{.name = "invalid index object",
               .index = "{}",
               .expected = ComponentIdIndex::Error::INVALID_SCHEMA},
      TestCase{.name = "invalid instances array",
               .index = R"({"instances": "abc"})",
               .expected = ComponentIdIndex::Error::INVALID_SCHEMA},
      TestCase{.name = "invalid entry object",
               .index = R"({"instances": ["abc"]})",
               .expected = ComponentIdIndex::Error::INVALID_SCHEMA},
      TestCase{.name = "missing instance_id entry",
               .index = R"({
                  "instances": [{
                    "appmgr_moniker": {
                      "url": "fuchsia-pkg://example.com",
                      "realm_path": ["sys"]
                    }
                  }]
                })",
               .expected = ComponentIdIndex::Error::INVALID_SCHEMA},
      // the instance_id should be a 64 hexchars.
      TestCase{.name = "invalid instance_id format",
               .index = R"({
                  "instances": [{
                    "instance_id": "8c90d44863ff67586cf6961",
                    "appmgr_moniker": {
                      "url": "fuchsia-pkg://example.com",
                      "realm_path": ["sys"]
                    }
                  }]
                })",
               .expected = ComponentIdIndex::Error::INVALID_INSTANCE_ID},
      // the instance_id should be a 64 hexchars.
      TestCase{.name = "duplicate instance IDs",
               .index = R"({
                  "instances" : [
                    {
                      "instance_id" : "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280",
                      "appmgr_moniker" :
                          {"realm_path" : ["sys"], "url" : "fuchsia-pkg://example.com/pkg#meta/component.cmx"}
                    },
                    {
                      "instance_id" : "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280",
                      "appmgr_moniker" : {
                        "realm_path" : [ "sys", "session" ],
                        "url" : "fuchsia-pkg://example.com/pkg#meta/component.cmx"
                      }
                    }
                  ]
                })",
               .expected = ComponentIdIndex::Error::DUPLICATE_INSTANCE_ID},
      TestCase{.name = "missing appmgr_moniker",
               .index = R"({
                  "instances": [{
                    "instance_id": "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280"
                  }]
                })",
               .expected = ComponentIdIndex::Error::INVALID_MONIKER},
      TestCase{.name = "duplicate moniker",
               .index = R"({
                  "instances" : [
                    {
                      "instance_id" : "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b351280",
                      "appmgr_moniker" :
                          {"realm_path" : ["sys"], "url" : "fuchsia-pkg://example.com/pkg#meta/component.cmx"}
                    },
                    {
                      "instance_id" : "8c90d44863ff67586cf6961081feba4f760decab8bbbee376a3bfbc77b35aaaa",
                      "appmgr_moniker" : {
                        "realm_path" : [ "sys" ],
                        "url" : "fuchsia-pkg://example.com/pkg#meta/component.cmx"
                      }
                    }
                  ]
                })",
               .expected = ComponentIdIndex::Error::DUPLICATE_MONIKER},
      TestCase{.name = "restrict_isolated_persistent_storage must be bool",
               .index = R"({
        "appmgr_restrict_isolated_persistent_storage": "should not be a string",
        "instances": []
      })",
               .expected = ComponentIdIndex::Error::INVALID_SCHEMA},
  };

  for (auto& test_case : test_cases) {
    auto result = ComponentIdIndex::CreateFromIndexContents(test_case.index);
    ASSERT_TRUE(result.is_error()) << "succeeded unexpectedly: " << test_case.name;
    EXPECT_EQ(test_case.expected, result.error()) << "failed: " << test_case.name;
  }
}

}  // namespace
}  // namespace component
