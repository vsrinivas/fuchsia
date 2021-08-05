// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "gmock/gmock.h"
#include "lib/sys/cpp/testing/test_with_environment_fixture.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/substitute.h"

namespace {

using ::fxl::Substitute;
using sys::testing::EnclosingEnvironment;
using ::testing::UnorderedElementsAre;
using namespace inspect::testing;

constexpr char kTestComponent[] =
    "fuchsia-pkg://fuchsia.com/dart-inspect-vmo-test-writer#meta/"
    "dart-inspect-vmo-test-writer.cmx";
constexpr char kTestProcessName[] = "dart-inspect-vmo-test-writer.cmx";

class InspectTest : public gtest::TestWithEnvironmentFixture {
 protected:
  InspectTest() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kTestComponent;

    environment_ = CreateNewEnclosingEnvironment("test", CreateServices());
    environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopWithTimeoutOrUntil([&ready] { return ready; }, zx::sec(100));
    if (!ready) {
      printf("The output directory is not ready\n");
    }
  }
  ~InspectTest() { CheckShutdown(); }

  void CheckShutdown() {
    controller_->Kill();
    bool done = false;
    controller_.events().OnTerminated = [&done](int64_t code,
                                                fuchsia::sys::TerminationReason reason) {
      ASSERT_EQ(fuchsia::sys::TerminationReason::EXITED, reason);
      done = true;
    };
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&done] { return done; }, zx::sec(100)));
  }

  // Open the root object connection on the given sync pointer.
  // Returns ZX_OK on success.
  fpromise::result<fuchsia::io::FileSyncPtr, zx_status_t> OpenInspectVmoFile(
      const std::string& file_name) {
    files::Glob glob(Substitute("/hub/r/test/*/c/*/*/c/$0/*/out/diagnostics/$1.inspect",
                                kTestProcessName, file_name));
    if (glob.size() == 0) {
      printf("Size == 0\n");
      return fpromise::error(ZX_ERR_NOT_FOUND);
    }

    fuchsia::io::FileSyncPtr file;
    auto status = fdio_open(std::string(*glob.begin()).c_str(), fuchsia::io::OPEN_RIGHT_READABLE,
                            file.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      printf("Status bad %d\n", status);
      return fpromise::error(status);
    }

    EXPECT_TRUE(file.is_bound());

    return fpromise::ok(std::move(file));
  }

  fpromise::result<zx::vmo, zx_status_t> DescribeInspectVmoFile(
      const fuchsia::io::FileSyncPtr& file) {
    fuchsia::io::NodeInfo info;
    auto status = file->Describe(&info);
    if (status != ZX_OK) {
      printf("get failed\n");
      return fpromise::error(status);
    }

    if (!info.is_vmofile()) {
      printf("not a vmofile");
      return fpromise::error(ZX_ERR_NOT_FOUND);
    }

    return fpromise::ok(std::move(info.vmofile().vmo));
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(InspectTest, ReadHierarchy) {
  auto open_file_result(InspectTest::OpenInspectVmoFile("root"));
  ASSERT_TRUE(open_file_result.is_ok());
  fuchsia::io::FileSyncPtr file(open_file_result.take_value());
  auto describe_file_result = InspectTest::DescribeInspectVmoFile(file);
  ASSERT_TRUE(describe_file_result.is_ok());
  zx::vmo vmo(describe_file_result.take_value());
  auto read_file_result = inspect::ReadFromVmo(std::move(vmo));
  ASSERT_TRUE(read_file_result.is_ok());
  inspect::Hierarchy hierarchy = read_file_result.take_value();

  // TODO(36155): Remove this once root migration is complete.
  auto* real_hierarchy = hierarchy.GetByPath({"root"});
  if (real_hierarchy == nullptr) {
    real_hierarchy = &hierarchy;
  }

  EXPECT_THAT(
      *real_hierarchy,
      AllOf(
          NodeMatches(NameMatches("root")),
          ChildrenMatch(UnorderedElementsAre(
              AllOf(NodeMatches(AllOf(NameMatches("t1"),
                                      PropertyList(UnorderedElementsAre(
                                          StringIs("version", "1.0"),
                                          ByteVectorIs("frame", std::vector<uint8_t>({0, 0, 0})),
                                          IntIs("value", -10), BoolIs("active", true))))),
                    ChildrenMatch(UnorderedElementsAre(
                        NodeMatches(AllOf(NameMatches("item-0x0"),
                                          PropertyList(UnorderedElementsAre(IntIs("value", 10))))),
                        NodeMatches(AllOf(NameMatches("item-0x1"),
                                          PropertyList(UnorderedElementsAre(IntIs("value", 100)))))

                            ))),
              AllOf(NodeMatches(AllOf(NameMatches("t2"),
                                      PropertyList(UnorderedElementsAre(
                                          StringIs("version", "1.0"),
                                          ByteVectorIs("frame", std::vector<uint8_t>({0, 0, 0})),
                                          IntIs("value", -10), BoolIs("active", true))))),
                    ChildrenMatch(UnorderedElementsAre(
                        NodeMatches(AllOf(NameMatches("item-0x2"),
                                          PropertyList(UnorderedElementsAre(IntIs("value", 4)))))))

                        )))));
}

TEST_F(InspectTest, DynamicGeneratesNewHierarchy) {
  auto open_file_result(OpenInspectVmoFile("digits_of_numbers"));
  ASSERT_TRUE(open_file_result.is_ok());
  fuchsia::io::FileSyncPtr file(open_file_result.take_value());

  std::vector<std::string> increments_value;
  std::vector<std::string> doubles_value;
  auto expectInspectOnDemandVmoFile = [&]() {
    auto describe_file_result(DescribeInspectVmoFile(file));
    ASSERT_TRUE(describe_file_result.is_ok());
    zx::vmo vmo(describe_file_result.take_value());
    auto read_file_result = inspect::ReadFromVmo(std::move(vmo));
    ASSERT_TRUE(read_file_result.is_ok());
    inspect::Hierarchy hierarchy = read_file_result.take_value();

    // TODO(36155): Remove this once root migration is complete.
    auto* real_hierarchy = hierarchy.GetByPath({"root"});
    if (real_hierarchy == nullptr) {
      real_hierarchy = &hierarchy;
    }

    EXPECT_THAT(*real_hierarchy,
                AllOf(NodeMatches(NameMatches("root")),
                      ChildrenMatch(UnorderedElementsAre(
                          NodeMatches(AllOf(  // child one
                              NameMatches("increments"),
                              PropertyList(UnorderedElementsAre(
                                  StringIs("value", ::testing::Truly([&](const std::string& val) {
                                             increments_value.push_back(val);
                                             return true;
                                           })))))),
                          NodeMatches(AllOf(  // child two
                              NameMatches("doubles"),
                              PropertyList(UnorderedElementsAre(
                                  StringIs("value", ::testing::Truly([&](const std::string& val) {
                                             doubles_value.push_back(val);
                                             return true;
                                           }))))))))));
  };

  expectInspectOnDemandVmoFile();
  expectInspectOnDemandVmoFile();

  ASSERT_EQ(2u, increments_value.size());
  ASSERT_EQ(2u, doubles_value.size());

  EXPECT_NE(increments_value[0], increments_value[1]);
  EXPECT_NE(doubles_value[0], doubles_value[1]);
}
}  // namespace
