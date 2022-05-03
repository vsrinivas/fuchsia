// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/crash_register.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/promise.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/timekeeper/test_clock.h"

namespace forensics {
namespace crash_reports {
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
class CrashRegisterTest : public UnitTestFixture {
 public:
  CrashRegisterTest()
      : info_context_(
            std::make_shared<InfoContext>(&InspectRoot(), &clock_, dispatcher(), services())) {}

  void SetUp() override { MakeNewCrashRegister(); }

 protected:
  void Upsert(const std::string& component_url, CrashReportingProduct product) {
    crash_register_->Upsert(component_url, std::move(product));
  }

  void Upsert(const std::string& component_url, CrashReportingProduct product,
              CrashRegister::UpsertWithAckCallback callback) {
    crash_register_->UpsertWithAck(component_url, std::move(product), std::move(callback));
  }

  bool HasProduct(const std::string& program_name) const {
    return crash_register_->HasProduct(program_name);
  }

  Product GetProduct(const std::string& program_name) const {
    return crash_register_->GetProduct(program_name);
  }

  std::string RegisterJsonPath() { return files::JoinPath(tmp_dir_.path(), "register.json"); }

  std::string ReadRegisterJson() {
    std::string json;
    files::ReadFileToString(RegisterJsonPath(), &json);
    return json;
  }

  void MakeNewCrashRegister() {
    crash_register_ = std::make_unique<CrashRegister>(info_context_, RegisterJsonPath());
  }

 private:
  timekeeper::TestClock clock_;
  files::ScopedTempDir tmp_dir_;
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
  EXPECT_EQ(ReadRegisterJson(), R"({
    "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx": {
        "name": "some name",
        "version": "some version",
        "channel": "some channel"
    }
})");
}

TEST_F(CrashRegisterTest, UpsertWithAck_Basic) {
  CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");

  bool success{false};
  Upsert(kComponentUrl, std::move(product), [&] { success = true; });
  ASSERT_TRUE(success);

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
  EXPECT_EQ(ReadRegisterJson(), R"({
    "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx": {
        "name": "some name",
        "version": "some version",
        "channel": "some channel"
    }
})");
}

TEST_F(CrashRegisterTest, Upsert_NoInsertOnMissingProductName) {
  CrashReportingProduct product;
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  EXPECT_THAT(InspectTree(),
              ChildrenMatch(Not(Contains(NodeMatches(NameMatches("crash_register"))))));
  EXPECT_TRUE(ReadRegisterJson().empty());
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
  EXPECT_EQ(ReadRegisterJson(), R"({
    "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx": {
        "name": "some other name",
        "version": "some other version",
        "channel": "some other channel"
    }
})");
}

TEST_F(CrashRegisterTest, GetProduct_NoUpsert) {
  EXPECT_FALSE(HasProduct("some program name"));
  EXPECT_TRUE(ReadRegisterJson().empty());
}

TEST_F(CrashRegisterTest, GetProduct_FromUpsert) {
  CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  const auto expected = Product{
      .name = "some name",
      .version = std::string("some version"),
      .channel = std::string("some channel"),
  };
  EXPECT_THAT(GetProduct(kComponentUrl), expected);
  EXPECT_EQ(ReadRegisterJson(), R"({
    "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx": {
        "name": "some name",
        "version": "some version",
        "channel": "some channel"
    }
})");
}

TEST_F(CrashRegisterTest, GetProduct_DifferentUpsert) {
  CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  EXPECT_FALSE(HasProduct("some program name"));
  EXPECT_EQ(ReadRegisterJson(), R"({
    "fuchsia-pkg://fuchsia.com/my-pkg#meta/my-component.cmx": {
        "name": "some name",
        "version": "some version",
        "channel": "some channel"
    }
})");
}

TEST_F(CrashRegisterTest, BuildDefaultProduct) {
  {
    Product actual{
        .name = std::string("Fuchsia"),
        .version = Error::kMissingValue,
        .channel = Error::kMissingValue,
    };

    CrashRegister::AddVersionAndChannel(actual, {});

    const Product expected{
        .name = std::string("Fuchsia"),
        .version = Error::kMissingValue,
        .channel = Error::kMissingValue,
    };
    EXPECT_EQ(actual, expected);
  }

  {
    Product actual{
        .name = std::string("Fuchsia"),
        .version = Error::kMissingValue,
        .channel = Error::kMissingValue,
    };

    CrashRegister::AddVersionAndChannel(actual, {
                                                    {feedback::kBuildVersionKey, "some version"},
                                                });

    const Product expected{
        .name = std::string("Fuchsia"),
        .version = std::string("some version"),
        .channel = Error::kMissingValue,
    };
    EXPECT_EQ(actual, expected);
  }

  {
    Product actual{
        .name = std::string("Fuchsia"),
        .version = Error::kMissingValue,
        .channel = Error::kMissingValue,
    };

    CrashRegister::AddVersionAndChannel(
        actual, {
                    {feedback::kBuildVersionKey, "some version"},
                    {feedback::kSystemUpdateChannelCurrentKey, "some channel"},
                });

    const Product expected{
        .name = std::string("Fuchsia"),
        .version = std::string("some version"),
        .channel = std::string("some channel"),
    };
    EXPECT_EQ(actual, expected);
  }
}

TEST_F(CrashRegisterTest, ReinitializesFromJson) {
  constexpr char kOtherComponentUrl[] =
      "fuchsia-pkg://fuchsia.com/my-other-pkg#meta/my-other-component.cmx";

  CrashReportingProduct product;
  product.set_name("some name");
  product.set_version("some version");
  product.set_channel("some channel");
  Upsert(kComponentUrl, std::move(product));

  CrashReportingProduct another_product;
  another_product.set_name("some other name");
  another_product.set_version("some other version");
  another_product.set_channel("some other channel");
  Upsert(kComponentUrl, std::move(another_product));

  CrashReportingProduct yet_another_product;
  yet_another_product.set_name("yet another name");
  yet_another_product.set_version("yet another version");
  Upsert(kOtherComponentUrl, std::move(yet_another_product));

  MakeNewCrashRegister();
  auto expected = Product{
      .name = "some other name",
      .version = std::string("some other version"),
      .channel = std::string("some other channel"),
  };
  EXPECT_THAT(GetProduct(kComponentUrl), expected);

  expected = Product{
      .name = "yet another name",
      .version = std::string("yet another version"),
      .channel = Error::kMissingValue,

  };
  EXPECT_THAT(GetProduct(kOtherComponentUrl), expected);
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
