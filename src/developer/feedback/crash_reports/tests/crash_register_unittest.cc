// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crash_reports/crash_register.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/crash_reports/info/info_context.h"
#include "src/developer/feedback/crash_reports/product.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/timekeeper/test_clock.h"

namespace feedback {
namespace {

using fuchsia::feedback::CrashReportingProduct;
using inspect::testing::ChildrenMatch;
using inspect::testing::NameMatches;
using inspect::testing::NodeMatches;
using inspect::testing::PropertyList;
using inspect::testing::StringIs;
using testing::Not;
using testing::UnorderedElementsAreArray;

constexpr char kComponentUrl[] = "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx";

// Unit-tests the server of fuchsia.feedback.CrashReportingProductRegister.
//
// This does not test the environment service. It directly instantiates the class, without
// connecting through FIDL.
class CrashRegisterTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  CrashRegisterTest() : UnitTestFixture(), CobaltTestFixture(/*unit_test_fixture=*/this) {}

  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    info_context_ =
        std::make_shared<InfoContext>(&inspector_->GetRoot(), clock_, dispatcher(), services());
    crash_register_ = std::make_unique<CrashRegister>(info_context_);

    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    RunLoopUntilIdle();
  }

 protected:
  void Upsert(const std::string& component_url, CrashReportingProduct product) {
    crash_register_->Upsert(component_url, std::move(product));
    RunLoopUntilIdle();
  }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_->DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

 private:
  timekeeper::TestClock clock_;
  std::unique_ptr<inspect::Inspector> inspector_;
  std::shared_ptr<InfoContext> info_context_;
  std::unique_ptr<CrashRegister> crash_register_;
};

TEST_F(CrashRegisterTest, Upsert_Basic) {
  CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("crash_register")),
                                 ChildrenMatch(Contains(AllOf(
                                     NodeMatches(NameMatches("mappings")),
                                     ChildrenMatch(UnorderedElementsAreArray({
                                         NodeMatches(AllOf(NameMatches(kComponentUrl),
                                                           PropertyList(UnorderedElementsAreArray({
                                                               StringIs("name", "some name"),
                                                               StringIs("version", "some version"),
                                                               StringIs("channel", "some channel"),
                                                           })))),
                                     })))))))));
}

TEST_F(CrashRegisterTest, Upsert_NoInsertOnMissingProductName) {
  CrashReportingProduct product;
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Not(Contains(NodeMatches(NameMatches("crash_register"))))));
}

TEST_F(CrashRegisterTest, Upsert_UpdateIfSameComponentUrl) {
  CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  EXPECT_THAT(InspectTree(), ChildrenMatch(Contains(AllOf(
                                 NodeMatches(NameMatches("crash_register")),
                                 ChildrenMatch(Contains(AllOf(
                                     NodeMatches(NameMatches("mappings")),
                                     ChildrenMatch(UnorderedElementsAreArray({
                                         NodeMatches(AllOf(NameMatches(kComponentUrl),
                                                           PropertyList(UnorderedElementsAreArray({
                                                               StringIs("name", "some name"),
                                                               StringIs("version", "some version"),
                                                               StringIs("channel", "some channel"),
                                                           })))),
                                     })))))))));

  CrashReportingProduct another_product;
  another_product.set_name("some other name");
  another_product.set_version("some other version");
  another_product.set_channel("some other channel");
  Upsert(kComponentUrl, std::move(another_product));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Contains(
                  AllOf(NodeMatches(NameMatches("crash_register")),
                        ChildrenMatch(Contains(AllOf(
                            NodeMatches(NameMatches("mappings")),
                            ChildrenMatch(UnorderedElementsAreArray({
                                NodeMatches(AllOf(NameMatches(kComponentUrl),
                                                  PropertyList(UnorderedElementsAreArray({
                                                      StringIs("name", "some other name"),
                                                      StringIs("version", "some other version"),
                                                      StringIs("channel", "some other channel"),
                                                  })))),
                            })))))))));
}

}  // namespace
}  // namespace feedback
