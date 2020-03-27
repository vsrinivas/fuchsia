// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <lib/fit/result.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"
#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/timekeeper/test_clock.h"

namespace feedback {
namespace {

using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using inspect::testing::UintIs;
using testing::ElementsAre;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

constexpr char kCrashServerUrl[] = "localhost:1234";

class CrashpadAgentTest : public UnitTestFixture {
 protected:
  void SetUpAgent() {
    Config config = {/*crash_server=*/
                     {
                         /*upload_policy=*/CrashServerConfig::UploadPolicy::ENABLED,
                         /*url=*/std::make_unique<std::string>(kCrashServerUrl),
                     }};
    inspector_ = std::make_unique<inspect::Inspector>();
    info_context_ =
        std::make_shared<InfoContext>(&inspector_->GetRoot(), clock_, dispatcher(), services());
    agent_ = CrashpadAgent::TryCreate(dispatcher(), services(), clock_, info_context_,
                                      std::move(config));
    FX_CHECK(agent_);
  }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

 private:
  std::unique_ptr<inspect::Inspector> inspector_;
  timekeeper::TestClock clock_;
  std::shared_ptr<InfoContext> info_context_;
  std::unique_ptr<CrashpadAgent> agent_;
};

TEST_F(CrashpadAgentTest, Check_InitialInspectTree) {
  SetUpAgent();
  EXPECT_THAT(
      InspectTree(),
      ChildrenMatch(UnorderedElementsAre(
          AllOf(NodeMatches(NameMatches("config")),
                ChildrenMatch(ElementsAre(NodeMatches(
                    AllOf(NameMatches(kCrashServerKey),
                          PropertyList(UnorderedElementsAreArray({
                              StringIs(kCrashServerUploadPolicyKey,
                                       ToString(CrashServerConfig::UploadPolicy::ENABLED)),
                              StringIs(kCrashServerUrlKey, kCrashServerUrl),
                          }))))))),
          NodeMatches(AllOf(NameMatches("database"),
                            PropertyList(ElementsAre(UintIs("max_crashpad_database_size_in_kb",
                                                            kCrashpadDatabaseMaxSizeInKb))))),
          NodeMatches(AllOf(NameMatches("settings"),
                            PropertyList(ElementsAre(StringIs(
                                "upload_policy", ToString(Settings::UploadPolicy::ENABLED)))))),
          NodeMatches(NameMatches("reports")), NodeMatches(NameMatches("queue")))));
}

}  // namespace
}  // namespace feedback
