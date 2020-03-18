// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/test/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/hwinfo/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/update/channel/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/feedback_agent/tests/zx_object_util.h"
#include "src/developer/feedback/testing/fake_cobalt.h"
#include "src/developer/feedback/testing/gmatchers.h"
#include "src/developer/feedback/utils/archive.h"
#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/developer/feedback/utils/metrics_registry.cb.h"
#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/uuid/uuid.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/schema.h"

namespace feedback {
namespace {

using fuchsia::feedback::Attachment;
using fuchsia::feedback::ComponentDataRegisterSyncPtr;
using fuchsia::feedback::Data;
using fuchsia::feedback::DataProvider_GetData_Result;
using fuchsia::feedback::DataProviderSyncPtr;
using fuchsia::feedback::DeviceIdProvider_GetId_Result;
using fuchsia::feedback::DeviceIdProviderSyncPtr;
using fuchsia::feedback::ImageEncoding;
using fuchsia::feedback::Screenshot;
using fuchsia::hwinfo::BoardInfo;
using fuchsia::hwinfo::BoardPtr;
using fuchsia::hwinfo::ProductInfo;
using fuchsia::hwinfo::ProductPtr;
using testing::UnorderedElementsAreArray;

class LogListener : public fuchsia::logger::LogListener {
 public:
  LogListener(std::shared_ptr<sys::ServiceDirectory> services) : binding_(this) {
    binding_.Bind(log_listener_.NewRequest());

    fuchsia::logger::LogPtr logger = services->Connect<fuchsia::logger::Log>();
    logger->Listen(std::move(log_listener_), /*options=*/nullptr);
  }

  bool HasLogs() { return has_logs_; }

 private:
  // |fuchsia::logger::LogListener|
  void LogMany(::std::vector<fuchsia::logger::LogMessage> log) { has_logs_ = true; }
  void Log(fuchsia::logger::LogMessage log) { has_logs_ = true; }
  void Done() { FXL_NOTIMPLEMENTED(); }

  fidl::Binding<fuchsia::logger::LogListener> binding_;
  fuchsia::logger::LogListenerPtr log_listener_;
  bool has_logs_ = false;
};

// Smoke-tests the real environment service for the fuchsia.feedback.DataProvider FIDL interface,
// connecting through FIDL.
class FeedbackAgentIntegrationTest : public sys::testing::TestWithEnvironment {
 public:
  void SetUp() override {
    environment_services_ = sys::ServiceDirectory::CreateFromNamespace();
    fake_cobalt_ = std::make_unique<FakeCobalt>(environment_services_);
  }

  void TearDown() override {
    if (inspect_test_app_controller_) {
      TerminateInspectTestApp();
    }
  }

 protected:
  // Makes sure the component serving fuchsia.logger.Log is up and running as the DumpLogs() request
  // could time out on machines where the component is too slow to start.
  //
  // Syslog are generally handled by a single logger that implements two protocols:
  //   (1) fuchsia.logger.LogSink to write syslog messages
  //   (2) fuchsia.logger.Log to read syslog messages and kernel log messages.
  // Returned syslog messages are restricted to the ones that were written using its LogSink while
  // kernel log messages are the same for all loggers.
  //
  // In this integration test, we inject a "fresh copy" of archivist.cmx for fuchsia.logger.Log so
  // we can retrieve the syslog messages. But we do _not_ inject that same archivist.cmx for
  // fuchsia.logger.LogSink as it would swallow all the error and warning messages the other
  // injected services could produce and make debugging really hard. Therefore, the injected
  // archivist.cmx does not have any syslog messages and will only have the global kernel log
  // messages.
  //
  // When archivist.cmx spawns, it will start collecting asynchronously kernel log messages. But if
  // DumpLogs() is called "too soon", it will immediately return empty logs instead of waiting on
  // the kernel log collection (fxb/4665), resulting in a flaky test (fxb/8303). We thus spawn
  // archivist.cmx on advance and wait for it to have at least one message before running the actual
  // test.
  void WaitForLogger() {
    LogListener log_listener(environment_services_);
    RunLoopUntil([&log_listener] { return log_listener.HasLogs(); });
  }

  // Makes sure the component serving fuchsia.update.channel.Provider is up and running as the
  // GetCurrent() request could time out on machines where the component is too slow to start.
  void WaitForChannelProvider() {
    fuchsia::update::channel::ProviderSyncPtr channel_provider;
    environment_services_->Connect(channel_provider.NewRequest());
    std::string unused;
    ASSERT_EQ(channel_provider->GetCurrent(&unused), ZX_OK);
  }

  // Makes sure there is at least one component in the test environment that exposes some Inspect
  // data.
  //
  // This is useful as we are excluding system_objects paths from the Inspect discovery and the test
  // component itself only has a system_objects Inspect node.
  void WaitForInspect() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = "fuchsia-pkg://fuchsia.com/feedback_agent_tests#meta/inspect_test_app.cmx";
    environment_ = CreateNewEnclosingEnvironment("inspect_test_app_environment", CreateServices());
    environment_->CreateComponent(std::move(launch_info),
                                  inspect_test_app_controller_.NewRequest());
    bool ready = false;
    inspect_test_app_controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });

    // Additionally wait for the component to appear in the observer's output.
    async::Executor executor(dispatcher());

    inspect::contrib::ArchiveReader reader(
        environment_services_->Connect<fuchsia::diagnostics::Archive>(),
        {"inspect_test_app_environment/inspect_test_app.cmx:root"});

    bool done = false;
    executor.schedule_task(reader.SnapshotInspectUntilPresent({"inspect_test_app.cmx"})
                               .then([&](fit::result<std::vector<inspect::contrib::DiagnosticsData>,
                                                     std::string>& unused) { done = true; }));
    RunLoopUntil([&done] { return done; });
  }

  // Makes sure the component serving fuchsia.hwinfo.BoardInfo is up and running as the
  // GetInfo() request could time out on machines where the component is too slow to start.
  void WaitForBoardProvider() {
    fuchsia::hwinfo::BoardPtr board_provider;
    environment_services_->Connect(board_provider.NewRequest());

    bool ready = false;
    board_provider->GetInfo([&](BoardInfo board_info) { ready = true; });
    RunLoopUntil([&ready] { return ready; });
  }

  // Makes sure the component serving fuchsia.hwinfo.ProductInfo is up and running as the
  // GetInfo() request could time out on machines where the component is too slow to start.
  void WaitForProductProvider() {
    fuchsia::hwinfo::ProductPtr product_provider;
    environment_services_->Connect(product_provider.NewRequest());

    bool ready = false;
    product_provider->GetInfo([&](ProductInfo product_info) { ready = true; });
    RunLoopUntil([&ready] { return ready; });
  }

 private:
  void TerminateInspectTestApp() {
    inspect_test_app_controller_->Kill();
    bool is_inspect_test_app_terminated = false;
    inspect_test_app_controller_.events().OnTerminated =
        [&is_inspect_test_app_terminated](int64_t code, fuchsia::sys::TerminationReason reason) {
          FXL_CHECK(reason == fuchsia::sys::TerminationReason::EXITED);
          is_inspect_test_app_terminated = true;
        };
    RunLoopUntil([&is_inspect_test_app_terminated] { return is_inspect_test_app_terminated; });
  }

 protected:
  std::shared_ptr<sys::ServiceDirectory> environment_services_;

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr inspect_test_app_controller_;

 protected:
  std::unique_ptr<FakeCobalt> fake_cobalt_;
};

TEST_F(FeedbackAgentIntegrationTest, ComponentDataRegister_Upsert_SmokeTest) {
  ComponentDataRegisterSyncPtr data_register;
  environment_services_->Connect(data_register.NewRequest());

  ASSERT_EQ(data_register->Upsert({}), ZX_OK);
}

// We use VK_TEST instead of the regular TEST macro because Scenic needs Vulkan to operate properly
// and take a screenshot. Note that calls to Scenic hang indefinitely for headless devices so this
// test assumes the device has a display like the other Scenic tests, see SCN-1281.
VK_TEST_F(FeedbackAgentIntegrationTest, DataProvider_GetScreenshot_SmokeTest) {
  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  std::unique_ptr<Screenshot> out_screenshot;
  ASSERT_EQ(data_provider->GetScreenshot(ImageEncoding::PNG, &out_screenshot), ZX_OK);
  // We cannot expect a particular payload in the response because Scenic might return a screenshot
  // or not depending on which device the test runs.
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

TEST_F(FeedbackAgentIntegrationTest, DataProvider_GetData_CheckKeys) {
  // We make sure the components serving the services GetData() connects to are up and running.
  WaitForLogger();
  WaitForChannelProvider();
  WaitForInspect();
  WaitForBoardProvider();
  WaitForProductProvider();

  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  fit::result<Data, zx_status_t> result = std::move(out_result);
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();

  // We cannot expect a particular value for each annotation or attachment because values might
  // depend on which device the test runs (e.g., board name) or what happened prior to running this
  // test (e.g., logs). But we should expect the keys to be present.
  ASSERT_TRUE(data.has_annotations());
  EXPECT_THAT(data.annotations(), testing::ElementsAreArray({
                                      MatchesKey(kAnnotationBuildBoard),
                                      MatchesKey(kAnnotationBuildIsDebug),
                                      MatchesKey(kAnnotationBuildLatestCommitDate),
                                      MatchesKey(kAnnotationBuildProduct),
                                      MatchesKey(kAnnotationBuildVersion),
                                      MatchesKey(kAnnotationChannel),
                                      MatchesKey(kAnnotationDeviceBoardName),
                                      MatchesKey(kAnnotationDeviceFeedbackId),
                                      MatchesKey(kAnnotationDeviceUptime),
                                      MatchesKey(kAnnotationDeviceUTCTime),
                                      MatchesKey(kAnnotationHardwareBoardName),
                                      MatchesKey(kAnnotationHardwareBoardRevision),
                                      MatchesKey(kAnnotationHardwareProductLanguage),
                                      MatchesKey(kAnnotationHardwareProductLocaleList),
                                      MatchesKey(kAnnotationHardwareProductManufacturer),
                                      MatchesKey(kAnnotationHardwareProductModel),
                                      MatchesKey(kAnnotationHardwareProductName),
                                      MatchesKey(kAnnotationHardwareProductRegulatoryDomain),
                                      MatchesKey(kAnnotationHardwareProductSKU),
                                  }));

  ASSERT_TRUE(data.has_attachment_bundle());
  const auto& attachment_bundle = data.attachment_bundle();
  EXPECT_STREQ(attachment_bundle.key.c_str(), kAttachmentBundle);
  std::vector<Attachment> unpacked_attachments;
  ASSERT_TRUE(Unpack(attachment_bundle.value, &unpacked_attachments));
  ASSERT_THAT(unpacked_attachments, testing::UnorderedElementsAreArray({
                                        MatchesKey(kAttachmentAnnotations),
                                        MatchesKey(kAttachmentBuildSnapshot),
                                        MatchesKey(kAttachmentInspect),
                                        MatchesKey(kAttachmentLogKernel),
                                        MatchesKey(kAttachmentLogSystem),
                                    }));

  std::string inspect_json;
  for (const auto& attachment : unpacked_attachments) {
    if (attachment.key != kAttachmentInspect) {
      continue;
    }
    ASSERT_TRUE(fsl::StringFromVmo(attachment.value, &inspect_json));
  }
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

TEST_F(FeedbackAgentIntegrationTest, DataProvider_GetData_CheckCobalt) {
  // We make sure the components serving the services GetData() connects to are up and running.
  WaitForLogger();
  WaitForChannelProvider();
  WaitForInspect();
  WaitForBoardProvider();
  WaitForProductProvider();

  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  fit::result<Data, zx_status_t> result = std::move(out_result);
  ASSERT_TRUE(result.is_ok());
  EXPECT_THAT(fake_cobalt_->GetAllEventsOfType<BugreportGenerationFlow>(
                  /*num_expected=*/1u, fuchsia::cobalt::test::LogMethod::LOG_ELAPSED_TIME),
              UnorderedElementsAreArray({
                  BugreportGenerationFlow::kSuccess,
              }));
}

TEST_F(FeedbackAgentIntegrationTest,
       DataProvider_GetData_ExtraAnnotationsFromComponentDataRegister) {
  // We make sure the components serving the services GetData() connects to are up and running.
  WaitForLogger();
  WaitForChannelProvider();
  WaitForInspect();
  WaitForBoardProvider();
  WaitForProductProvider();

  DataProviderSyncPtr data_provider;
  environment_services_->Connect(data_provider.NewRequest());

  ComponentDataRegisterSyncPtr data_register;
  environment_services_->Connect(data_register.NewRequest());

  fuchsia::feedback::ComponentData extra_data;
  extra_data.set_namespace_("namespace");
  extra_data.set_annotations({
      {"k", "v"},
  });
  ASSERT_EQ(data_register->Upsert(std::move(extra_data)), ZX_OK);

  DataProvider_GetData_Result out_result;
  ASSERT_EQ(data_provider->GetData(&out_result), ZX_OK);

  fit::result<Data, zx_status_t> result = std::move(out_result);
  ASSERT_TRUE(result.is_ok());

  const Data& data = result.value();
  ASSERT_TRUE(data.has_annotations());
  EXPECT_THAT(data.annotations(), testing::Contains(MatchesAnnotation("namespace.k", "v")));
}

TEST_F(FeedbackAgentIntegrationTest, DeviceIdProvider_GetId_CheckValue) {
  DeviceIdProviderSyncPtr device_id_provider;
  environment_services_->Connect(device_id_provider.NewRequest());

  DeviceIdProvider_GetId_Result out_result;
  ASSERT_EQ(device_id_provider->GetId(&out_result), ZX_OK);

  ASSERT_TRUE(out_result.is_response());
  EXPECT_TRUE(uuid::IsValid(out_result.response().ResultValue_()));
}

}  // namespace
}  // namespace feedback
