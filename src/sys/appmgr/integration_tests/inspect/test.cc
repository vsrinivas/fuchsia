// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include "gmock/gmock.h"
#include "src/lib/files/glob.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"

namespace component {
namespace {

using ::fxl::Substitute;
using sys::testing::EnclosingEnvironment;
using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;
using ByteVector = ::component::Property::ByteVector;

const char kTestComponent[] =
    "fuchsia-pkg://fuchsia.com/inspect_integration_tests#meta/"
    "inspect_test_app.cmx";
const char kTestProcessName[] = "inspect_test_app.cmx";

class InspectTest : public sys::testing::TestWithEnvironment {
 protected:
  InspectTest() {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = kTestComponent;

    environment_ = CreateNewEnclosingEnvironment("test", CreateServices());
    environment_->CreateComponent(std::move(launch_info), controller_.NewRequest());
    bool ready = false;
    controller_.events().OnDirectoryReady = [&ready] { ready = true; };
    RunLoopUntil([&ready] { return ready; });
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
    RunLoopUntil([&done] { return done; });
  }

  // Open the root object connection on the given sync pointer.
  // Returns ZX_OK on success.
  zx_status_t GetInspectConnection(fuchsia::inspect::deprecated::InspectSyncPtr* out_ptr) {
    files::Glob glob(Substitute("/hub/r/test/*/c/$0/*/out/diagnostics", kTestProcessName));
    if (glob.size() == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    std::string path = Substitute("$0/$1", std::string(*glob.begin()),
                                  std::string(fuchsia::inspect::deprecated::Inspect::Name_));

    return fdio_service_connect(path.c_str(), out_ptr->NewRequest().TakeChannel().release());
  }

  // Traverse from the current location of ptr down the object tree to a child
  // with the given name.
  // Returns true on success, otherwise false is returned and ptr is not
  // changed.
  bool Traverse(fuchsia::inspect::deprecated::InspectSyncPtr* ptr, const std::string& name) {
    fuchsia::inspect::deprecated::InspectSyncPtr child;
    bool ret;
    (*ptr)->OpenChild(name, child.NewRequest(), &ret);
    if (ret) {
      *ptr = std::move(child);
    }
    return ret;
  }

 private:
  std::unique_ptr<EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(InspectTest, InspectTopLevel) {
  fuchsia::inspect::deprecated::InspectSyncPtr inspect;
  ASSERT_EQ(ZX_OK, GetInspectConnection(&inspect));

  std::vector<std::string> children;
  inspect->ListChildren(&children);
  EXPECT_THAT(children, UnorderedElementsAre("table-t1", "table-t2", "lazy_child"));
}

MATCHER_P2(StringProperty, name, value, "") {
  return arg.value.is_str() && arg.key == name && arg.value.str() == value;
}

MATCHER_P2(VectorProperty, name, value, "") {
  return arg.value.is_bytes() && arg.key == name && arg.value.bytes() == value;
}

MATCHER_P2(UIntMetric, name, value, "") {
  return arg.key == name && arg.value.is_uint_value() && arg.value.uint_value() == (uint64_t)value;
}

MATCHER_P2(IntMetric, name, value, "") {
  return arg.key == name && arg.value.is_int_value() && arg.value.int_value() == (int64_t)value;
}

TEST_F(InspectTest, InspectOpenRead) {
  fuchsia::inspect::deprecated::InspectSyncPtr inspect;

  ASSERT_EQ(ZX_OK, GetInspectConnection(&inspect));
  ASSERT_TRUE(Traverse(&inspect, "table-t1"));

  std::vector<std::string> children;
  ASSERT_EQ(ZX_OK, inspect->ListChildren(&children));
  EXPECT_THAT(children, UnorderedElementsAre("item-0x0", "item-0x1"));

  fuchsia::inspect::deprecated::Object obj;
  ASSERT_EQ(ZX_OK, inspect->ReadData(&obj));
  EXPECT_EQ("table-t1", obj.name);
  EXPECT_THAT(obj.properties,
              UnorderedElementsAre(StringProperty("version", "1.0"),
                                   VectorProperty("frame", ByteVector({0x10, 0x00, 0x10})),
                                   VectorProperty("\x10\x10", ByteVector({0x00, 0x00, 0x00}))));
  EXPECT_THAT(obj.metrics,
              UnorderedElementsAre(UIntMetric("item_size", 32), IntMetric("\x10", -10)));

  ASSERT_EQ(ZX_OK, GetInspectConnection(&inspect));
  ASSERT_TRUE(Traverse(&inspect, "table-t2"));

  children.clear();
  ASSERT_EQ(ZX_OK, inspect->ListChildren(&children));
  EXPECT_THAT(children, UnorderedElementsAre("item-0x2", "table-subtable"));

  obj = fuchsia::inspect::deprecated::Object();
  ASSERT_EQ(ZX_OK, inspect->ReadData(&obj));
  EXPECT_EQ("table-t2", obj.name);
  fuchsia::inspect::deprecated::Object subtable;
  fuchsia::inspect::deprecated::InspectSyncPtr child_request;
  bool ok = false;
  ASSERT_EQ(ZX_OK, inspect->OpenChild("table-subtable", child_request.NewRequest(), &ok));
  ASSERT_TRUE(ok);
  child_request->ReadData(&subtable);
  EXPECT_EQ(subtable.name, "table-subtable");
  children.clear();
  ASSERT_EQ(ZX_OK, child_request->ListChildren(&children));
  EXPECT_THAT(children, UnorderedElementsAre("item-0x3"));
  EXPECT_THAT(subtable.metrics,
              UnorderedElementsAre(UIntMetric("item_size", 16), IntMetric("\x10", -10)));

  ASSERT_EQ(ZX_OK, GetInspectConnection(&inspect));
  fuchsia::inspect::deprecated::InspectSyncPtr lazy_child;
  bool open_ok;
  inspect->OpenChild("lazy_child", lazy_child.NewRequest(), &open_ok);
  ASSERT_TRUE(open_ok);
  obj = fuchsia::inspect::deprecated::Object();
  lazy_child->ReadData(&obj);
  EXPECT_THAT(obj.properties, UnorderedElementsAre(StringProperty("version", "1")));
}

}  // namespace
}  // namespace component
