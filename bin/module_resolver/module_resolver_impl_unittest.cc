// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/module_resolver_impl.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "peridot/lib/testing/test_with_message_loop.h"
#include "peridot/public/lib/module_resolver/cpp/formatting.h"

namespace maxwell {
namespace {

const char* kManifest = R"END(
[
  {
    "binary": "module1",
    "local_name": "module1",
    "verb": "Navigate",
    "noun_constraints": [
      {
        "name": "start",
        "types": [ "foo", "bar" ]
      },
      {
        "name": "destination",
        "types": [ "baz" ]
      }
    ]
  },
  {
    "binary": "module2",
    "local_name": "module2",
    "verb": "Navigate",
    "noun_constraints": [
      {
        "name": "start",
        "types": [ "frob" ]
      }
    ]
  },
  {
    "binary": "module3",
    "local_name": "module3",
    "verb": "Exist",
    "noun_constraints": [
      {
        "name": "with",
        "types": [ "companionCube" ]
      }
    ]
  }
]
)END";

class ModuleResolverImplTest : public modular::testing::TestWithMessageLoop {
 public:
  void SetUp() override {
    // Set up a temp dir for the repository.
    char temp_dir[] = "/tmp/module_manifest_repo_XXXXXX";
    FXL_CHECK(mkdtemp(temp_dir)) << strerror(errno);
    repo_dir_ = temp_dir;

    WriteManifestFile("manifest", kManifest);
    ResetRepository();
  }
  void TearDown() override {
    // Clean up.
    resolver_.reset();
    for (const auto& path : manifests_written_) {
      remove(path.c_str());
    }

    rmdir(repo_dir_.c_str());
  }

 protected:
  void ResetRepository() {
    resolver_.reset(new ModuleResolverImpl(repo_dir_));
    // |resolver_| has a thread that posts tasks to our MessageLoop that need
    // to be processed for initialization. Give them a chance to post and for
    // |resolver_| to pick them up.
    RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(200));
  }

  void WriteManifestFile(const std::string& name, const char* contents) {
    const auto path = repo_dir_ + '/' + name;
    manifests_written_.push_back(path);

    FXL_CHECK(files::WriteFile(path, contents, strlen(contents)));
  }

  void FindModules(modular::DaisyPtr daisy) {
    auto scoring_info = modular::ResolverScoringInfo::New();

    got_response_ = false;
    resolver_->FindModules(std::move(daisy), nullptr /* scoring_info */,
                           [this](const modular::FindModulesResultPtr& result) {
                             got_response_ = true;
                             result_ = result.Clone();
                           });

    ASSERT_TRUE(got_response_) << daisy;
  }

  const fidl::Array<modular::ModuleResolverResultPtr>& results() const {
    return result_->modules;
  }

  std::vector<std::string> manifests_written_;
  std::string repo_dir_;
  std::unique_ptr<ModuleResolverImpl> resolver_;

  bool got_response_;
  modular::FindModulesResultPtr result_;
};

TEST_F(ModuleResolverImplTest, Null) {
  auto daisy = modular::Daisy::New();
  daisy->verb = "no matchy!";

  FindModules(std::move(daisy));

  // The Resolver currently always returns a fallback Module.
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("resolution_failed", results()[0]->module_id);
}

TEST_F(ModuleResolverImplTest, SimpleVerb) {
  auto daisy = modular::Daisy::New();
  daisy->verb = "Navigate";

  FindModules(std::move(daisy));

  ASSERT_EQ(2lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);
  EXPECT_EQ("module2", results()[1]->module_id);
}

}  // namespace
}  // namespace maxwell
