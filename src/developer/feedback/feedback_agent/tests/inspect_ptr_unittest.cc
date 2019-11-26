// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/result.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/zx/time.h>

#include <string>
#include <vector>

#include "src/lib/fsl/vmo/strings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace feedback {
namespace {

class CollectInspectDataTest : public sys::testing::TestWithEnvironment {
 public:
  CollectInspectDataTest()
      : executor_(dispatcher()),
        collection_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        collection_executor_(collection_loop_.dispatcher()) {}

  void SetUp() override { ASSERT_EQ(collection_loop_.StartThread("collection-thread"), ZX_OK); }

  void TearDown() override {
    std::cout << "TearDown START" << std::endl << std::flush;
    if (inspect_test_app_controller_) {
      TerminateInspectTestApp();
    }
    // To make sure there are no more running tasks on |collection_executor_| when it gets
    // destroyed, cf. fxb/39880.
    collection_loop_.Shutdown();
    std::cout << "TearDown END" << std::endl << std::flush;
  }

 protected:
  // Injects a test app that exposes some Inspect data in the test environment.
  //
  // Useful to guarantee there is a component within the environment that exposes Inspect data as
  // we are excluding system_objects paths from the Inspect discovery and the test component itself
  // only has a system_objects Inspect node.
  void InjectInspectTestApp(const std::vector<std::string>& args = {}) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/feedback_agent_tests#meta/inspect_test_app.cmx";
    launch_info.arguments = args;
    environment_ = CreateNewEnclosingEnvironment("inspect_test_app_environment", CreateServices());
    environment_->CreateComponent(std::move(launch_info),
                                  inspect_test_app_controller_.NewRequest());
    bool ready = false;
    inspect_test_app_controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
  }

  fit::result<fuchsia::mem::Buffer> CollectInspectData(const zx::duration timeout = zx::sec(1)) {
    fit::result<fuchsia::mem::Buffer> result;
    bool has_result = false;
    executor_.schedule_task(
        feedback::CollectInspectData(dispatcher(), timeout, &collection_executor_)
            .then([&result, &has_result](fit::result<fuchsia::mem::Buffer>& res) {
              result = std::move(res);
              has_result = true;
            }));
    RunLoopUntil([&has_result] { return has_result; });
    return result;
  }

 private:
  void TerminateInspectTestApp() {
    std::cout << "TerminateInspectTestApp START" << std::endl << std::flush;
    inspect_test_app_controller_->Kill();
    bool is_inspect_test_app_terminated = false;
    inspect_test_app_controller_.events().OnTerminated =
        [&is_inspect_test_app_terminated](int64_t code, fuchsia::sys::TerminationReason reason) {
          FXL_CHECK(reason == fuchsia::sys::TerminationReason::EXITED);
          is_inspect_test_app_terminated = true;
        };
    RunLoopUntil([&is_inspect_test_app_terminated] { return is_inspect_test_app_terminated; });
    std::cout << "TerminateInspectTestApp END" << std::endl << std::flush;
  }

 protected:
  async::Executor executor_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr inspect_test_app_controller_;
  async::Loop collection_loop_;

 protected:
  async::Executor collection_executor_;
};

constexpr char kInspectJsonSchema[] = R"({
  "type": "array",
  "items": {
    "type": "object",
    "properties": {
      "path": {
        "type": "string"
      },
      "contents": {
        "type": "object"
      }
    },
    "required": [
      "path",
      "contents"
    ],
    "additionalProperties": false
  },
  "uniqueItems": true
})";

TEST_F(CollectInspectDataTest, Succeed_OneComponentExposesInspectData) {
  InjectInspectTestApp();

  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();

  ASSERT_TRUE(result.is_ok());

  const fuchsia::mem::Buffer& inspect = result.value();

  std::string inspect_json;
  ASSERT_TRUE(fsl::StringFromVmo(inspect, &inspect_json));
  ASSERT_FALSE(inspect_json.empty());

  // JSON verification.
  // We check that the output is a valid JSON and that it matches the schema.
  rapidjson::Document json;
  ASSERT_FALSE(json.Parse(inspect_json.c_str()).HasParseError());
  rapidjson::Document schema_json;
  ASSERT_FALSE(schema_json.Parse(kInspectJsonSchema).HasParseError());
  rapidjson::SchemaDocument schema(schema_json);
  rapidjson::SchemaValidator validator(schema);
  EXPECT_TRUE(json.Accept(validator));

  // We then check that we get the expected Inspect data for the injected test app.
  bool has_entry_for_test_app = false;
  for (const auto& obj : json.GetArray()) {
    const std::string path = obj["path"].GetString();
    if (path.find("inspect_test_app.cmx") != std::string::npos) {
      has_entry_for_test_app = true;
      const auto contents = obj["contents"].GetObject();
      ASSERT_TRUE(contents.HasMember("root"));
      const auto root = contents["root"].GetObject();
      ASSERT_TRUE(root.HasMember("obj1"));
      ASSERT_TRUE(root.HasMember("obj2"));
      const auto obj1 = root["obj1"].GetObject();
      const auto obj2 = root["obj2"].GetObject();
      ASSERT_TRUE(obj1.HasMember("version"));
      ASSERT_TRUE(obj2.HasMember("version"));
      EXPECT_STREQ(obj1["version"].GetString(), "1.0");
      EXPECT_STREQ(obj2["version"].GetString(), "1.0");
      ASSERT_TRUE(obj1.HasMember("value"));
      ASSERT_TRUE(obj2.HasMember("value"));
      EXPECT_EQ(obj1["value"].GetUint(), 100u);
      EXPECT_EQ(obj2["value"].GetUint(), 200u);
    }
  }
  EXPECT_TRUE(has_entry_for_test_app);
}

TEST_F(CollectInspectDataTest, Fail_NoComponentExposesInspectData) {
  fit::result<fuchsia::mem::Buffer> result = CollectInspectData();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectInspectDataTest, Fail_InspectDiscoveryTimeout) {
  std::cout << "Fail_InspectDiscoveryTimeout START" << std::endl << std::flush;
  // The test app exposes some Inspect data, but will be too busy to answer.
  InjectInspectTestApp({"--busy"});

  // The test will need to actually wait for the timeout so we don't put a value too high.
  fit::result<fuchsia::mem::Buffer> result = CollectInspectData(/*timeout=*/zx::msec(50));

  ASSERT_TRUE(result.is_error());
  std::cout << "Fail_InspectDiscoveryTimeout END" << std::endl << std::flush;
}

TEST_F(CollectInspectDataTest, Fail_CallCollectTwice) {
  const zx::duration unused_timeout = zx::sec(1);
  Inspect inspect(dispatcher(), &collection_executor_);
  executor_.schedule_task(inspect.Collect(unused_timeout));
  ASSERT_DEATH(inspect.Collect(unused_timeout),
               testing::HasSubstr("Collect() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback
