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

#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/snapshot_manager.h"
#include "src/developer/forensics/testing/stubs/loader.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
namespace {

const std::string kUrl{"http://www.foo.com"};
const Report kReport{
    /*report_id=*/0,          /*program_shortname=*/"program-shortname",
    /*annotations=*/{},
    /*attachments=*/{},       /*snapshot_uuid=*/"snapshot-uuid",
    /*minidump=*/std::nullopt};

class CrashServerTest : public gtest::TestLoopFixture {
 protected:
  CrashServerTest()
      : loader_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        loader_context_provider_(loader_loop_.dispatcher()),
        snapshot_manager_(dispatcher(), loader_context_provider_.public_service_directory(),
                          std::make_unique<timekeeper::TestClock>(), zx::min(0),
                          StorageSize::Bytes(0u), StorageSize::Bytes(0u)),
        tags_(),
        crash_server_(loader_context_provider_.public_service_directory(), kUrl, &snapshot_manager_,
                      &tags_) {
    FX_CHECK(loader_loop_.StartThread() == ZX_OK);
  }

  ~CrashServerTest() {
    // Shutdown |laoder_loop_| before running member variable destructors in case tasks on the loop
    // hold references to member variables.
    loader_loop_.Shutdown();
  }

  void SetUpLoader(std::vector<stubs::LoaderResponse> responses) {
    loader_server_ =
        std::make_unique<stubs::Loader>(loader_loop_.dispatcher(), std::move(responses));
    FX_CHECK(loader_context_provider_.context()->outgoing()->AddPublicService(
                 loader_server_->GetHandler()) == ZX_OK);
  }

  CrashServer& crash_server() { return crash_server_; }

 private:
  // |loader_server_| needs to run on a separate thread because |crash_server_| makes synchronous
  // FIDL calls.
  async::Loop loader_loop_;
  sys::testing::ComponentContextProvider loader_context_provider_;
  std::unique_ptr<stubs::Loader> loader_server_;

  SnapshotManager snapshot_manager_;
  LogTags tags_;
  CrashServer crash_server_;
};

TEST_F(CrashServerTest, Fails_OnError) {
  SetUpLoader({stubs::LoaderResponse::WithError(fuchsia::net::http::Error::DEADLINE_EXCEEDED)});

  std::string server_report_id;
  EXPECT_FALSE(crash_server().MakeRequest(kReport, &server_report_id));
}

TEST_F(CrashServerTest, Fails_StatusCodeBelow200) {
  SetUpLoader({stubs::LoaderResponse::WithError(199)});

  std::string server_report_id;
  EXPECT_FALSE(crash_server().MakeRequest(kReport, &server_report_id));
}

TEST_F(CrashServerTest, Fails_StatusCodeAbove203) {
  SetUpLoader({stubs::LoaderResponse::WithError(204)});

  std::string server_report_id;
  EXPECT_FALSE(crash_server().MakeRequest(kReport, &server_report_id));
}

TEST_F(CrashServerTest, ReadBodyOnSuccess) {
  SetUpLoader({
      stubs::LoaderResponse::WithBody(200, "body-200"),
      stubs::LoaderResponse::WithBody(201, "body-201"),
      stubs::LoaderResponse::WithBody(202, "body-202"),
      stubs::LoaderResponse::WithBody(203, "body-203"),
  });

  std::string server_report_id;
  EXPECT_TRUE(crash_server().MakeRequest(kReport, &server_report_id));
  EXPECT_EQ(server_report_id, "body-200");

  EXPECT_TRUE(crash_server().MakeRequest(kReport, &server_report_id));
  EXPECT_EQ(server_report_id, "body-201");

  EXPECT_TRUE(crash_server().MakeRequest(kReport, &server_report_id));
  EXPECT_EQ(server_report_id, "body-202");

  EXPECT_TRUE(crash_server().MakeRequest(kReport, &server_report_id));
  EXPECT_EQ(server_report_id, "body-203");
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
