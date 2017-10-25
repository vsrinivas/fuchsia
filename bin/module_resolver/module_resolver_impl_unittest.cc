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
    "verb": "com.google.fuchsia.navigate.v1",
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
    "verb": "com.google.fuchsia.navigate.v1",
    "noun_constraints": [
      {
        "name": "start",
        "types": [ "frob" ]
      },
      {
        "name": "destination",
        "types": [ "froozle" ]
      }
    ]
  },
  {
    "binary": "module3",
    "local_name": "module3",
    "verb": "com.google.fuchsia.exist.vinfinity",
    "noun_constraints": [
      {
        "name": "with",
        "types": [ "companionCube" ]
      }
    ]
  }
]
)END";

class DaisyBuilder {
 public:
  DaisyBuilder() : daisy(modular::Daisy::New()) {
    daisy->nouns.mark_non_null();
  }
  DaisyBuilder(std::string verb) : daisy(modular::Daisy::New()) {
    daisy->nouns.mark_non_null();
    SetVerb(verb);
  }

  modular::DaisyPtr get() { return std::move(daisy); }

  DaisyBuilder& SetVerb(std::string verb) {
    daisy->verb = verb;
    return *this;
  }

  // Creates a noun that's just Entity types.
  DaisyBuilder& AddNounTypes(std::string name, std::vector<std::string> types) {
    auto noun = modular::Noun::New();
    auto types_array = fidl::Array<fidl::String>::From(types);
    noun->set_entity_type(std::move(types_array));
    daisy->nouns.insert(name, std::move(noun));
    return *this;
  }

  // Creates a noun that's made of JSON content.
  DaisyBuilder& AddJsonNoun(std::string name, std::string json) {
    auto noun = modular::Noun::New();
    noun->set_json(json);
    daisy->nouns.insert(name, std::move(noun));
    return *this;
  }

 private:
  modular::DaisyPtr daisy;
};

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
    impl_.reset(new ModuleResolverImpl(repo_dir_));
    impl_->Connect(resolver_.NewRequest());

    // |impl_| has a thread that posts tasks to our MessageLoop that need to be
    // processed for initialization. Give them a chance to post and for |impl_|
    // to pick them up.
    // TODO(thatguy): To avoid potential flake: improve internals so that we
    // can wait on a real initialization condition on |impl|.
    RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(200));
  }

  void WriteManifestFile(const std::string& name, const char* contents) {
    const auto path = repo_dir_ + '/' + name;
    manifests_written_.push_back(path);

    FXL_CHECK(files::WriteFile(path, contents, strlen(contents)));
  }

  void FindModules(modular::DaisyPtr daisy) {
    auto scoring_info = modular::ResolverScoringInfo::New();

    bool got_response = false;
    resolver_->FindModules(
        std::move(daisy), nullptr /* scoring_info */,
        [this, &got_response](const modular::FindModulesResultPtr& result) {
          got_response = true;
          result_ = result.Clone();
        });
    RunLoopUntil([&got_response] { return got_response; });
    ASSERT_TRUE(got_response) << daisy;
  }

  const fidl::Array<modular::ModuleResolverResultPtr>& results() const {
    return result_->modules;
  }

  std::vector<std::string> manifests_written_;
  std::string repo_dir_;
  std::unique_ptr<ModuleResolverImpl> impl_;

  modular::ModuleResolverPtr resolver_;

  modular::FindModulesResultPtr result_;
};

#define ASSERT_DEFAULT_RESULT(results) \
  ASSERT_EQ(1lu, results.size()); \
  EXPECT_EQ("resolution_failed", results[0]->module_id);

TEST_F(ModuleResolverImplTest, Null) {
  auto daisy = DaisyBuilder("no matchy!").get();

  FindModules(std::move(daisy));

  // The Resolver currently always returns a fallback Module.
  ASSERT_DEFAULT_RESULT(results());
}

TEST_F(ModuleResolverImplTest, SimpleVerb) {
  auto daisy = DaisyBuilder("com.google.fuchsia.navigate.v1").get();

  FindModules(std::move(daisy));

  ASSERT_EQ(2lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);
  EXPECT_EQ("module2", results()[1]->module_id);
}

TEST_F(ModuleResolverImplTest, SimpleNounTypes) {
  // Either 'foo' or 'tangoTown' would be acceptible types. Only 'foo' will
  // actually match.
  auto daisy = DaisyBuilder("com.google.fuchsia.navigate.v1")
                   .AddNounTypes("start", {"foo", "tangoTown"})
                   .get();
  FindModules(std::move(daisy));
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);

  // This one will match one of the two noun constraints on module1, but not
  // both, so no match at all is expected.
  daisy = DaisyBuilder("com.google.fuchsia.navigate.v1")
              .AddNounTypes("start", {"foo", "tangoTown"})
              .AddNounTypes("destination", {"notbaz"})
              .get();
  FindModules(std::move(daisy));
  ASSERT_DEFAULT_RESULT(results());
}

TEST_F(ModuleResolverImplTest, SimpleJsonNouns) {
  // Same thing as above, but we'll use JSON with embedded type information and
  // should see the same exactly results.
  auto daisy = DaisyBuilder("com.google.fuchsia.navigate.v1")
                   .AddJsonNoun("start", R"({
                      "@type": [ "foo", "tangoTown" ],
                      "thecake": "is a lie"
                    })")
                   .AddJsonNoun("destination", R"({
                      "@type": "baz",
                      "really": "it is"
                    })")
                   .get();
  FindModules(std::move(daisy));
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);
  // TODO(thatguy): Validate that the initial_nouns content is correct.
}

}  // namespace
}  // namespace maxwell
