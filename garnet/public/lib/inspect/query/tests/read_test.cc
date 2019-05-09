// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/query/read.h"

#include <fuchsia/io/cpp/fidl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/inspect/inspect.h>
#include <lib/inspect/query/source.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <src/lib/fxl/strings/join_strings.h>

#include "fixture.h"
#include "fuchsia/inspect/cpp/fidl.h"
#include "lib/inspect/hierarchy.h"
#include "lib/inspect/query/location.h"
#include "lib/inspect/reader.h"
#include "lib/inspect/testing/inspect.h"

using namespace inspect::testing;

namespace {

class TestDataWrapper {
 public:
  explicit TestDataWrapper(inspect::Node object) : object_(std::move(object)) {
    version_ = object_.CreateStringProperty("version", "1.0");
    child_test_ = object_.CreateChild("test");
    count_ = child_test_.CreateIntMetric("count", 2);
  }

 private:
  inspect::Node object_;
  inspect::Node child_test_;
  inspect::StringProperty version_;
  inspect::IntMetric count_;
};

class ReadTest : public TestFixture {
 public:
  ReadTest()
      : tree_(inspector_.CreateTree("root")),
        fidl_dir_(component::ObjectDir::Make("root")),
        fidl_test_data_(inspect::Node(fidl_dir_)),
        vmo_test_data_(std::move(tree_.GetRoot())) {
    // Host a FIDL and VMO inspect interface under /test in the global
    // namespace.
    root_dir_.AddEntry(fuchsia::inspect::Inspect::Name_,
                       std::make_unique<vfs::Service>(
                           bindings_.GetHandler(fidl_dir_.object().get())));

    root_dir_.AddEntry("root.inspect",
                       std::make_unique<vfs::VmoFile>(
                           zx::unowned_vmo(tree_.GetVmo()), 0, 4096));

    fuchsia::io::DirectoryPtr ptr;
    root_dir_.Serve(
        fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
        ptr.NewRequest().TakeChannel());
    ZX_ASSERT(fdio_ns_get_installed(&ns_) == ZX_OK);
    ZX_ASSERT(fdio_ns_bind(ns_, "/test",
                           ptr.Unbind().TakeChannel().release()) == ZX_OK);
  }

  ~ReadTest() {
    if (ns_) {
      ZX_ASSERT(fdio_ns_unbind(ns_, "/test") == ZX_OK);
    }
  }

 protected:
  inspect::Inspector inspector_;
  inspect::Tree tree_;
  component::ObjectDir fidl_dir_;
  TestDataWrapper fidl_test_data_, vmo_test_data_;
  fidl::BindingSet<fuchsia::inspect::Inspect> bindings_;
  vfs::PseudoDir root_dir_;
  fdio_ns_t* ns_;
};

TEST_F(ReadTest, ReadLocations) {
  const std::vector<std::string> paths = {"/test/root.inspect", "/test"};

  for (const auto& path : paths) {
    fit::result<inspect::Source, std::string> result;

    SchedulePromise(
        inspect::ReadLocation(inspect::Location::Parse(path).take_value())
            .then([&](fit::result<inspect::Source, std::string>& res) {
              result = std::move(res);
            }));

    RunLoopUntil([&] { return !!result; });

    ASSERT_TRUE(result.is_ok())
        << "for " << path << " error " << result.error().c_str();
    EXPECT_THAT(
        result.take_value().GetHierarchy(),
        ::testing::AllOf(
            NodeMatches(::testing::AllOf(
                NameMatches("root"), PropertyList(::testing::ElementsAre(
                                         StringPropertyIs("version", "1.0"))))),
            ChildrenMatch(::testing::ElementsAre(NodeMatches(::testing::AllOf(
                NameMatches("test"), MetricList(::testing::ElementsAre(
                                         IntMetricIs("count", 2)))))))));
  }
}

TEST_F(ReadTest, ReadLocationsChild) {
  const std::vector<std::string> paths = {"/test/root.inspect#test",
                                          "/test#test"};

  for (const auto& path : paths) {
    fit::result<inspect::Source, std::string> result;

    SchedulePromise(
        inspect::ReadLocation(inspect::Location::Parse(path).take_value())
            .then([&](fit::result<inspect::Source, std::string>& res) {
              result = std::move(res);
            }));

    RunLoopUntil([&] { return !!result; });

    ASSERT_TRUE(result.is_ok())
        << "for " << path << " error " << result.error().c_str();
    EXPECT_THAT(
        result.take_value().GetHierarchy(),
        NodeMatches(::testing::AllOf(
            NameMatches("test"),
            MetricList(::testing::ElementsAre(IntMetricIs("count", 2))))));
  }
}

TEST_F(ReadTest, ReadLocationsError) {
  const std::vector<std::string> paths = {
      "/test/root.inspect#missing", "/test#missing", "/",
      "/test/missing.inspect",      "/test/missing",
  };

  for (const auto& path : paths) {
    fit::result<inspect::Source, std::string> result;

    SchedulePromise(
        inspect::ReadLocation(inspect::Location::Parse(path).take_value())
            .then([&](fit::result<inspect::Source, std::string>& res) {
              result = std::move(res);
            }));

    RunLoopUntil([&] { return !!result; });

    ASSERT_TRUE(result.is_error());
  }
}

}  // namespace
