// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_server.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/stubs/data_provider.h"
#include "src/developer/forensics/testing/stubs/loader.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

const std::string kUrl{"http://www.foo.com"};
const std::string kSnapshotUuid{"snapshot-uuid"};
const Report kReport{/*report_id=*/0,
                     /*program_shortname=*/"program-shortname",
                     /*annotations=*/
                     {
                         {"product", "some-product"},
                         {"version", "some-version"},
                     },
                     /*attachments=*/{},
                     /*snapshot_uuid=*/kSnapshotUuid,
                     /*minidump=*/std::nullopt};

class CrashServerTest : public UnitTestFixture {
 protected:
  CrashServerTest()
      : data_provider_server_(std::make_unique<stubs::DataProviderReturnsEmptySnapshot>()),
        annotation_manager_(dispatcher(), {}),
        tags_() {
    RunLoopUntilIdle();
  }

  void SetUpLoader(std::vector<stubs::LoaderResponse> responses) {
    loader_server_ = std::make_unique<stubs::Loader>(dispatcher(), std::move(responses));
    InjectServiceProvider(loader_server_.get());
    crash_server_ = std::make_unique<CrashServer>(dispatcher(), services(), kUrl, &tags_);
    RunLoopUntilIdle();
  }

  CrashServer& crash_server() { return *crash_server_; }
  Snapshot GetSnapshot() {
    // The presence annotations below are returned in the Snapshot from SnapshotStore::GetSnapshot
    // whenever a snapshot is not persisted
    return MissingSnapshot(annotation_manager_.ImmediatelyAvailable(),
                           {
                               {feedback::kDebugSnapshotErrorKey, "not persisted"},
                               {feedback::kDebugSnapshotPresentKey, "false"},
                           });
  }

  std::string LoaderLastRequestUrl() const { return loader_server_->LastRequestUrl(); }

 private:
  sys::testing::ComponentContextProvider loader_context_provider_;
  std::unique_ptr<stubs::Loader> loader_server_;

  std::unique_ptr<stubs::DataProviderBase> data_provider_server_;
  feedback::AnnotationManager annotation_manager_;
  LogTags tags_;
  std::unique_ptr<CrashServer> crash_server_;
};

TEST_F(CrashServerTest, UrlWithEncodedParameter) {
  SetUpLoader({
      stubs::LoaderResponse::WithBody(200, "body-200"),
      stubs::LoaderResponse::WithBody(201, "body-201"),
  });

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  crash_server().MakeRequest(
      kReport, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kSuccess);
  EXPECT_EQ(LoaderLastRequestUrl(), kUrl + "?product=some-product&version=some-version");

  upload_status = std::nullopt;
  const Report another_report{/*report_id=*/0,
                              /*program_shortname=*/"program-shortname",
                              /*annotations=*/
                              {
                                  {"product", "!product"},
                                  {"version", "#version"},
                              },
                              /*attachments=*/{},
                              /*snapshot_uuid=*/kSnapshotUuid,
                              /*minidump=*/std::nullopt};

  crash_server().MakeRequest(
      another_report, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kSuccess);
  EXPECT_EQ(LoaderLastRequestUrl(), kUrl + "?product=%21product&version=%23version");
}

TEST_F(CrashServerTest, Fails_OnError) {
  SetUpLoader({stubs::LoaderResponse::WithError(fuchsia::net::http::Error::CONNECT)});

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  crash_server().MakeRequest(
      kReport, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kFailure);
}

TEST_F(CrashServerTest, Fails_OnTimeout) {
  SetUpLoader({stubs::LoaderResponse::WithError(fuchsia::net::http::Error::DEADLINE_EXCEEDED)});

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  crash_server().MakeRequest(
      kReport, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kTimedOut);
}

TEST_F(CrashServerTest, Fails_StatusCodeBelow200) {
  SetUpLoader({stubs::LoaderResponse::WithError(199)});

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  crash_server().MakeRequest(
      kReport, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kFailure);
}

TEST_F(CrashServerTest, Fails_StatusCodeAbove203) {
  SetUpLoader({stubs::LoaderResponse::WithError(204)});

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  crash_server().MakeRequest(
      kReport, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kFailure);
}

TEST_F(CrashServerTest, Fails_UploadThrottled) {
  SetUpLoader({stubs::LoaderResponse::WithError(429)});

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  crash_server().MakeRequest(
      kReport, GetSnapshot(),
      [&](CrashServer::UploadStatus status, std::string) { upload_status = status; });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kThrottled);
}

TEST_F(CrashServerTest, ReadBodyOnSuccess) {
  SetUpLoader({
      stubs::LoaderResponse::WithBody(200, "body-200"),
      stubs::LoaderResponse::WithBody(201, "body-201"),
      stubs::LoaderResponse::WithBody(202, "body-202"),
      stubs::LoaderResponse::WithBody(203, "body-203"),
  });

  std::optional<CrashServer::UploadStatus> upload_status{std::nullopt};
  std::optional<std::string> server_report_id;
  crash_server().MakeRequest(kReport, GetSnapshot(),
                             [&](CrashServer::UploadStatus status, std::string response) {
                               upload_status = status;
                               server_report_id = response;
                             });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kSuccess);

  ASSERT_TRUE(server_report_id.has_value());
  EXPECT_EQ(server_report_id.value(), "body-200");

  upload_status = std::nullopt;
  server_report_id = std::nullopt;

  crash_server().MakeRequest(kReport, GetSnapshot(),
                             [&](CrashServer::UploadStatus status, std::string response) {
                               upload_status = status;
                               server_report_id = response;
                             });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kSuccess);

  ASSERT_TRUE(server_report_id.has_value());
  EXPECT_EQ(server_report_id.value(), "body-201");

  upload_status = std::nullopt;
  server_report_id = std::nullopt;

  crash_server().MakeRequest(kReport, GetSnapshot(),
                             [&](CrashServer::UploadStatus status, std::string response) {
                               upload_status = status;
                               server_report_id = response;
                             });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kSuccess);

  ASSERT_TRUE(server_report_id.has_value());
  EXPECT_EQ(server_report_id.value(), "body-202");

  upload_status = std::nullopt;
  server_report_id = std::nullopt;

  crash_server().MakeRequest(kReport, GetSnapshot(),
                             [&](CrashServer::UploadStatus status, std::string response) {
                               upload_status = status;
                               server_report_id = response;
                             });
  RunLoopUntilIdle();

  ASSERT_TRUE(upload_status.has_value());
  EXPECT_EQ(upload_status.value(), CrashServer::UploadStatus::kSuccess);

  ASSERT_TRUE(server_report_id.has_value());
  EXPECT_EQ(server_report_id.value(), "body-203");
}

TEST_F(CrashServerTest, PreparesAnnotationsErrorSnapshot) {
  const Report report{/*report_id=*/0,
                      /*program_shortname=*/"program-shortname",
                      /*annotations=*/
                      {
                          {"key1", "value1"},
                          {"key2", "value2"},
                      },
                      /*attachments=*/{},
                      /*snapshot_uuid=*/kSnapshotUuid,
                      /*minidump=*/std::nullopt};

  const feedback::Annotations annotations({
      {"key2", "value2.1"},
      {"key3", "value3"},
  });

  const feedback::Annotations presence_annotations({
      {"key3", "value3.1"},
      {"key4", "value4"},

  });

  EXPECT_THAT(CrashServer::PrepareAnnotations(
                  report, MissingSnapshot(feedback::Annotations(), presence_annotations)),
              UnorderedElementsAreArray({
                  Pair("key1", "value1"),
                  Pair("key2", "value2"),
                  Pair("key3", "value3.1"),
                  Pair("key4", "value4"),
              }));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
