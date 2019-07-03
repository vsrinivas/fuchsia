// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/inspect_deprecated/inspect.h>
#include <lib/inspect_deprecated/query/source.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <src/lib/files/file.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/join_strings.h>
#include <src/lib/fxl/strings/substitute.h>

#include "fixture.h"
#include "fuchsia/io/cpp/fidl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/inspect_deprecated/hierarchy.h"
#include "lib/inspect_deprecated/query/location.h"
#include "lib/inspect_deprecated/reader.h"
#include "lib/inspect_deprecated/testing/inspect.h"

using namespace inspect_deprecated::testing;

namespace {

class TestDataWrapper {
 public:
  explicit TestDataWrapper(inspect_deprecated::Node object) : object_(std::move(object)) {
    version_ = object_.CreateStringProperty("version", "1.0");
    child_test_ = object_.CreateChild("test");
    count_ = child_test_.CreateIntMetric("count", 2);
    nested_child_ = child_test_.CreateChild("nested");
  }

 private:
  inspect_deprecated::Node object_;
  inspect_deprecated::Node child_test_;
  inspect_deprecated::Node nested_child_;
  inspect_deprecated::StringProperty version_;
  inspect_deprecated::IntMetric count_;
};

void CheckHierarchyMatches(const inspect_deprecated::ObjectHierarchy& hierarchy) {
  EXPECT_THAT(hierarchy,
              ::testing::AllOf(
                  NodeMatches(::testing::AllOf(
                      NameMatches("root"),
                      PropertyList(::testing::ElementsAre(StringPropertyIs("version", "1.0"))))),
                  ChildrenMatch(::testing::ElementsAre(::testing::AllOf(
                      ChildrenMatch(::testing::ElementsAre(NodeMatches(NameMatches("nested")))),
                      NodeMatches(::testing::AllOf(
                          NameMatches("test"),
                          MetricList(::testing::ElementsAre(IntMetricIs("count", 2))))))))));
}

class SourceTestFidl : public TestFixture {
 public:
  SourceTestFidl()
      : fidl_dir_(component::ObjectDir::Make("root")),
        test_data_(inspect_deprecated::Node(fidl_dir_)),
        binding_(fidl_dir_.object().get()) {
    binding_.Bind(ptr_.NewRequest().TakeChannel());
  }

  fit::result<inspect_deprecated::Source, std::string> MakeFromPath(std::string path,
                                                                    int depth = -1) {
    fit::result<inspect_deprecated::Source, std::string> result;
    SchedulePromise(inspect_deprecated::Source::MakeFromFidl(
                        inspect_deprecated::Location::Parse(path).take_value(),
                        inspect_deprecated::ObjectReader(std::move(ptr_)), depth)
                        .then([&result](fit::result<inspect_deprecated::Source, std::string>& res) {
                          result = std::move(res);
                        }));

    RunLoopUntil([&result] { return !!result; });

    return result;
  }

  const std::string RootPath = "/test";

 protected:
  component::ObjectDir fidl_dir_;
  TestDataWrapper test_data_;
  fidl::Binding<fuchsia::inspect::Inspect> binding_;
  fuchsia::inspect::InspectPtr ptr_;
};

class SourceTestVmo : public TestFixture {
 public:
  SourceTestVmo()
      : inspector_(),
        tree_(inspector_.CreateTree("root")),
        vmo_file_(zx::unowned_vmo(tree_.GetVmo()), 0, 4096),
        test_data_(std::move(tree_.GetRoot())) {
    ZX_ASSERT(vmo_file_.Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                              file_ptr_.NewRequest().TakeChannel()) == ZX_OK);
  }

  fit::result<inspect_deprecated::Source, std::string> MakeFromPath(std::string path,
                                                                    int depth = -1) {
    fit::result<inspect_deprecated::Source, std::string> result;
    SchedulePromise(
        inspect_deprecated::Source::MakeFromVmo(
            inspect_deprecated::Location::Parse(path).take_value(), std::move(file_ptr_), depth)
            .then([&result](fit::result<inspect_deprecated::Source, std::string>& res) {
              result = std::move(res);
            }));

    RunLoopUntil([&result] { return !!result; });

    return result;
  }

  const std::string RootPath = "/test/root.inspect";

 protected:
  inspect_deprecated::Inspector inspector_;
  inspect_deprecated::Tree tree_;
  vfs::VmoFile vmo_file_;
  TestDataWrapper test_data_;
  fuchsia::io::FilePtr file_ptr_;
};

class SourceTestFile : public SourceTestVmo {
 public:
  fit::result<inspect_deprecated::Source, std::string> MakeFromPath(std::string path,
                                                                    int depth = -1) {
    fit::result<std::string, std::string> path_or = WriteFromVmo(path);
    if (path_or.is_error()) {
      return fit::error(path_or.error());
    }

    fuchsia::io::FilePtr file_backed_ptr;
    zx_status_t status = fdio_open(path_or.value().c_str(), fuchsia::io::OPEN_RIGHT_READABLE,
                                   file_backed_ptr.NewRequest().TakeChannel().release());
    ZX_ASSERT(status == ZX_OK && file_backed_ptr.is_bound());

    fit::result<inspect_deprecated::Source, std::string> result;
    SchedulePromise(inspect_deprecated::Source::MakeFromVmo(
                        inspect_deprecated::Location::Parse(path).take_value(),
                        std::move(file_backed_ptr), depth)
                        .then([&result](fit::result<inspect_deprecated::Source, std::string>& res) {
                          result = std::move(res);
                        }));

    RunLoopUntil([&result] { return !!result; });

    return result;
  }

 protected:
  // Writes the contents of the VMO backed by test data into a file at |path|.
  // Returns the resulting file name, or a string error.
  fit::result<std::string, std::string> WriteFromVmo(const std::string& path) {
    const zx::vmo& vmo = tree_.GetVmo();
    uint64_t vmo_size;
    zx_status_t status = vmo.get_size(&vmo_size);
    if (status != ZX_OK) {
      return fit::error("could not get VMO size");
    }
    const fbl::Array<uint8_t> buf(new uint8_t[vmo_size], vmo_size);
    status = vmo.read(buf.begin(), 0, vmo_size);
    if (status != ZX_OK) {
      return fit::error("could not read from VMO");
    }
    const char* f = reinterpret_cast<const char*>(buf.begin());  // Known safe.
    if (!files::WriteFile(path, f, buf.size())) {
      return fit::error(fxl::Substitute("Could not write: $0", path));
    }
    return fit::ok(path);
  }

  const std::string RootPath = "/tmp/file.inspect";
};

template <typename T>
class SourceTest : public T {};

using SourceTestTypes = ::testing::Types<SourceTestFidl, SourceTestVmo, SourceTestFile>;
TYPED_TEST_SUITE(SourceTest, SourceTestTypes);

TYPED_TEST(SourceTest, MakeDefault) {
  // TODO(FLK-186): Reenable this test.
  GTEST_SKIP();
  auto result = this->MakeFromPath(this->RootPath);
  ASSERT_TRUE(result.is_ok());
  auto source = result.take_value();
  CheckHierarchyMatches(source.GetHierarchy());
}

TYPED_TEST(SourceTest, MakeDepth0) {
  // TODO(FLK-186): Reenable this test.
  GTEST_SKIP();
  auto result = this->MakeFromPath(this->RootPath, 0);
  ASSERT_TRUE(result.is_ok());

  EXPECT_THAT(result.take_value().GetHierarchy(),
              ::testing::AllOf(NodeMatches(PropertyList(::testing::SizeIs(1))),
                               ChildrenMatch(::testing::SizeIs(0))));
}

TYPED_TEST(SourceTest, MakeDepth1) {
  // TODO(FLK-186): Reenable this test.
  GTEST_SKIP();
  auto result = this->MakeFromPath(this->RootPath, 1);
  ASSERT_TRUE(result.is_ok());

  EXPECT_THAT(result.take_value().GetHierarchy(),
              ::testing::AllOf(ChildrenMatch(::testing::ElementsAre(::testing::AllOf(
                  NodeMatches(NameMatches("test")), ::ChildrenMatch(::testing::SizeIs(0)))))));
}

TYPED_TEST(SourceTest, MakeWithPath) {
  // TODO(FLK-186): Reenable this test.
  GTEST_SKIP();
  auto result = this->MakeFromPath(fxl::Concatenate({this->RootPath, "#test"}));
  ASSERT_TRUE(result.is_ok());

  EXPECT_THAT(
      result.take_value().GetHierarchy(),
      ::testing::AllOf(NodeMatches(MetricList(::testing::ElementsAre(IntMetricIs("count", 2)))),
                       ChildrenMatch(::testing::SizeIs(1))));
}

// These tests only apply to the test prototypes in SourceTestErrorTypes.
template <typename T>
class SourceTestError : public T {};

using SourceTestErrorTypes = ::testing::Types<SourceTestFidl, SourceTestVmo>;
TYPED_TEST_SUITE(SourceTestError, SourceTestErrorTypes);

TYPED_TEST(SourceTestError, MakeError) {
  // TODO(FLK-186): Reenable this test.
  GTEST_SKIP();
  auto result = this->MakeFromPath(this->RootPath);
  ASSERT_TRUE(result.is_ok());
  result = this->MakeFromPath(this->RootPath);  // reusing the connection should fail.
  ASSERT_TRUE(result.is_error());
}

inspect_deprecated::ObjectHierarchy MakeNode(std::string name) {
  return inspect_deprecated::ObjectHierarchy(inspect_deprecated::hierarchy::Node(std::move(name)),
                                             {});
}

TEST(Source, VisitObjectsInHierarchy) {
  inspect_deprecated::ObjectHierarchy root = MakeNode("root");
  {
    auto child = MakeNode("child");
    child.children().emplace_back(MakeNode("nested"));
    root.children().emplace_back(std::move(child));
  }
  root.children().emplace_back(MakeNode("a_child"));

  auto source = inspect_deprecated::Source({}, std::move(root));

  std::vector<std::string> paths_visited;
  source.VisitObjectsInHierarchy([&](const std::vector<std::string>& path,
                                     const inspect_deprecated::ObjectHierarchy& hierarchy) {
    paths_visited.push_back(fxl::JoinStrings(path, "/"));
  });

  EXPECT_THAT(paths_visited, ::testing::ElementsAre("", "child", "child/nested", "a_child"));

  paths_visited.clear();
  source.SortHierarchy();
  source.VisitObjectsInHierarchy([&](const std::vector<std::string>& path,
                                     const inspect_deprecated::ObjectHierarchy& hierarchy) {
    paths_visited.push_back(fxl::JoinStrings(path, "/"));
  });

  EXPECT_THAT(paths_visited, ::testing::ElementsAre("", "a_child", "child", "child/nested"));
}

}  // namespace
