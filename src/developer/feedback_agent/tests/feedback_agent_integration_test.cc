// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <stdint.h>
#include <zircon/errors.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h"
#include "src/developer/feedback_agent/tests/zx_object_util.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace fuchsia {
namespace feedback {
namespace {

// Returns true if gMock |arg|.key matches |expected_key|.
MATCHER_P(MatchesKey, expected_key,
          "matches an element with key '" + std::string(expected_key) + "'") {
  return arg.key == expected_key;
}

// Smoke-tests the real environment service for the
// fuchsia.feedback.DataProvider FIDL interface, connecting through FIDL.
class FeedbackAgentIntegrationTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    environment_services_ = ::sys::ServiceDirectory::CreateFromNamespace();
  }

 protected:
  std::shared_ptr<::sys::ServiceDirectory> environment_services_;
};

// We use VK_TEST instead of the regular TEST macro because Scenic needs Vulkan
// to operate properly and take a screenshot. Note that calls to Scenic hang
// indefinitely for headless devices so this test assumes the device has a
// display like the other Scenic tests, see SCN-1281.
VK_TEST_F(FeedbackAgentIntegrationTest, GetScreenshot_SmokeTest) {
  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  std::unique_ptr<Screenshot> out_screenshot;
  ASSERT_EQ(data_provider->GetScreenshot(ImageEncoding::PNG, &out_screenshot),
            ZX_OK);
  // We cannot expect a particular payload in the response because Scenic might
  // return a screenshot or not depending on which device the test runs.
}

class LogListener : public fuchsia::logger::LogListener {
 public:
  LogListener(std::shared_ptr<::sys::ServiceDirectory> services)
      : binding_(this) {
    binding_.Bind(log_listener_.NewRequest());

    fuchsia::logger::LogPtr logger = services->Connect<fuchsia::logger::Log>();
    logger->Listen(std::move(log_listener_), /*options=*/nullptr);
  }

  bool HasLogs() { return has_logs_; }

 private:
  // |fuchsia::logger::LogListener|
  void LogMany(::std::vector<fuchsia::logger::LogMessage> log) {
    has_logs_ = true;
  }
  void Log(fuchsia::logger::LogMessage log) { has_logs_ = true; }
  void Done() { FXL_NOTIMPLEMENTED(); }

  fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  bool has_logs_ = false;
};

TEST_F(FeedbackAgentIntegrationTest, GetData_CheckKeys) {
  // One of the attachments are the syslog. Syslog are generally handled by a
  // single logger that implements two protocols: (1) fuchsia.logger.LogSink to
  // write syslog messages and (2) fuchsia.logger.Log to read syslog messages
  // and kernel log messages. Returned syslog messages are restricted to the
  // ones that were written using its LogSink while kernel log messages are the
  // same for all loggers.
  //
  // In this integration test, we inject a "fresh copy" of logger.cmx for
  // fuchsia.logger.Log so we can retrieve the syslog messages. But we do _not_
  // inject that same logger.cmx for fuchsia.logger.LogSink as it would swallow
  // all the error and warning messages the other injected services could
  // produce and make debugging really hard. Therefore, the injected logger.cmx
  // does not have any syslog messages and will only have the global kernel log
  // messages.
  //
  // When logger.cmx spawns, it will start collecting asynchronously kernel log
  // messages. But if DumpLogs() is called "too soon", it will immediately
  // return empty logs instead of waiting on the kernel log collection (CF-790),
  // resulting in a flaky test (FLK-179). We thus spawn logger.cmx on advance
  // and wait for it to have at least one message before running the actual
  // test.
  LogListener log_listener(environment_services_);
  RunLoopUntil([&log_listener] { return log_listener.HasLogs(); });

  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  ASSERT_TRUE(out_result.is_response());

  // We cannot expect a particular value for each annotation or attachment
  // because values might depend on which device the test runs (e.g., board
  // name) or what happened prior to running this test (e.g., logs). But we
  // should expect the keys to be present.
  ASSERT_TRUE(out_result.response().data.has_annotations());
  EXPECT_THAT(out_result.response().data.annotations(),
              testing::UnorderedElementsAreArray({
                  MatchesKey("device.board-name"),
                  MatchesKey("build.latest-commit-date"),
                  MatchesKey("build.version"),
                  MatchesKey("build.board"),
                  MatchesKey("build.product"),
              }));
  ASSERT_TRUE(out_result.response().data.has_attachments());
  EXPECT_THAT(out_result.response().data.attachments(),
              testing::UnorderedElementsAreArray({
                  MatchesKey("build.snapshot"),
                  MatchesKey("log.kernel"),
                  MatchesKey("log.system"),
                  MatchesKey("inspect"),
              }));
}

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

TEST_F(FeedbackAgentIntegrationTest, GetData_ValidInspectJson) {
  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  ASSERT_TRUE(out_result.is_response());
  ASSERT_TRUE(out_result.response().data.has_attachments());

  bool found_inspect_attachment = false;
  for (const auto& attachment : out_result.response().data.attachments()) {
    if (attachment.key.compare("inspect") != 0) {
      continue;
    }
    found_inspect_attachment = true;

    std::string inspect_str;
    ASSERT_TRUE(fsl::StringFromVmo(attachment.value, &inspect_str));
    ASSERT_FALSE(inspect_str.empty());

    // JSON verification.
    // We check that the output is a valid JSON and that it matches the schema.
    rapidjson::Document inspect_json;
    ASSERT_FALSE(inspect_json.Parse(inspect_str.c_str()).HasParseError());
    rapidjson::Document inspect_schema_json;
    ASSERT_FALSE(inspect_schema_json.Parse(kInspectJsonSchema).HasParseError());
    rapidjson::SchemaDocument schema(inspect_schema_json);
    rapidjson::SchemaValidator validator(schema);
    EXPECT_TRUE(inspect_json.Accept(validator));

    // We check that we get some Inspect data for the two components that are
    // guaranteed to be in the test environment: feedback_agent.cmx and
    // feedback_agent_integration_test.cmx.
    bool has_entry_for_feedback_agent = false;
    bool has_entry_for_feedback_agent_integration_test = false;
    for (const auto& obj : inspect_json.GetArray()) {
      const std::string path = obj["path"].GetString();
      if (path.find("feedback_agent.cmx") != std::string::npos) {
        has_entry_for_feedback_agent = true;
      } else if (path.find("feedback_agent_integration_test.cmx") !=
                 std::string::npos) {
        has_entry_for_feedback_agent_integration_test = true;
      }
    }
    EXPECT_TRUE(has_entry_for_feedback_agent);
    EXPECT_TRUE(has_entry_for_feedback_agent_integration_test);
  }
  EXPECT_TRUE(found_inspect_attachment);
}

// EXPECTs that there is a feedback_agent.cmx process running in a child job of
// the test environment job and that this process has
// |expected_num_data_providers| sibling processes.
void CheckNumberOfDataProviderProcesses(
    const uint32_t expected_num_data_providers) {
  // We want to check how many data_provider subprocesses feedback_agent has
  // spawned.
  //
  // The job and process hierarchy looks like this under the test environment:
  // j: 109762 env_for_test_42bc5f2a
  //   j: 109993
  //     p: 109998 feedback_agent_integration_test
  //   j: 112299
  //     p: 112304 vulkan_loader.cmx
  //   j: 115016
  //     p: 115021 feedback_agent.cmx
  //     p: 115022 /pkg/bin/data_provider
  //     p: 115023 /pkg/bin/data_provider
  //     p: 115024 /pkg/bin/data_provider
  //   j: 116540
  //     p: 116545 logger.cmx
  //
  // There is basically a job the for the test component and a job for each
  // injected service. The one of interest is feedback_agent.cmx and we check
  // the number of sibling processes named /pkg/bin/data_provider.

  fuchsia::sys::JobProviderSyncPtr job_provider;
  ASSERT_EQ(fdio_service_connect(
                "/hub/job", job_provider.NewRequest().TakeChannel().release()),
            ZX_OK);
  zx::job env_for_test_job;
  ASSERT_EQ(job_provider->GetJob(&env_for_test_job), ZX_OK);
  ASSERT_THAT(fsl::GetObjectName(env_for_test_job.get()),
              testing::StartsWith("env_for_test"));

  // Child jobs are for the test component and each injected service.
  auto child_jobs = GetChildJobs(env_for_test_job.get());
  ASSERT_GE(child_jobs.size(), 1u);

  uint32_t num_feedback_agents = 0u;
  for (const auto& child_job : child_jobs) {
    auto processes = GetChildProcesses(child_job.get());
    ASSERT_GE(processes.size(), 1u);

    bool contains_feedback_agent = false;
    uint32_t num_data_providers = 0u;
    for (const auto& process : processes) {
      const std::string process_name = fsl::GetObjectName(process.get());
      if (process_name == "feedback_agent.cmx") {
        contains_feedback_agent = true;
        num_feedback_agents++;
      } else if (process_name == "/pkg/bin/data_provider") {
        num_data_providers++;
      }
    }

    if (contains_feedback_agent) {
      EXPECT_EQ(num_data_providers, expected_num_data_providers);
    }
  }
  EXPECT_EQ(num_feedback_agents, 1u);
}

TEST_F(FeedbackAgentIntegrationTest, OneDataProviderPerRequest) {
  DataProviderSyncPtr data_provider_1;
  environment_services_->Connect(data_provider_1.NewRequest());
  // As the connection is asynchronous, we make a call with the SyncPtr to make
  // sure the connection is established and the process for the service spawned
  // before checking its existence.
  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider_1->GetData(&out_result), ZX_OK);
  CheckNumberOfDataProviderProcesses(1u);

  DataProviderSyncPtr data_provider_2;
  environment_services_->Connect(data_provider_2.NewRequest());
  ASSERT_EQ(data_provider_2->GetData(&out_result), ZX_OK);
  CheckNumberOfDataProviderProcesses(2u);

  DataProviderSyncPtr data_provider_3;
  environment_services_->Connect(data_provider_3.NewRequest());
  ASSERT_EQ(data_provider_3->GetData(&out_result), ZX_OK);
  CheckNumberOfDataProviderProcesses(3u);

  data_provider_1.Unbind();
  data_provider_2.Unbind();
  data_provider_3.Unbind();
  // Ideally we would check after each Unbind() that there is one less
  // data_provider process, but the process clean up is asynchronous.
}

}  // namespace
}  // namespace feedback
}  // namespace fuchsia
