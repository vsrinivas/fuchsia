// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component/cpp/markers.h>
#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.inspect/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fpromise/promise.h>
#include <lib/inspect/component/cpp/service.h>
#include <lib/inspect/component/cpp/testing.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

using inspect::Inspector;
using inspect::InspectSettings;
using inspect::testing::TreeClient;
using inspect::testing::TreeNameIteratorClient;

namespace {

class InspectServiceTest : public gtest::RealLoopFixture,
                           public testing::WithParamInterface<uint64_t> {
 public:
  InspectServiceTest()
      : executor_(dispatcher()),
        inspector_(Inspector(InspectSettings{.maximum_size = 268435456})) {}

 protected:
  inspect::Node& root() { return inspector_.GetRoot(); }

  TreeClient Connect() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_inspect::Tree>();
    inspect::TreeServer::StartSelfManagedServer(
        inspector_,
        inspect::TreeHandlerSettings{.snapshot_behavior = inspect::TreeServerSendPreference::Frozen(
                                         inspect::TreeServerSendPreference::Type::Live)},
        dispatcher(), std::move(endpoints->server));

    return TreeClient{std::move(endpoints->client), dispatcher()};
  }

  TreeClient ConnectPrivate() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_inspect::Tree>();
    inspect::TreeServer::StartSelfManagedServer(
        inspector_,
        inspect::TreeHandlerSettings{.snapshot_behavior =
                                         inspect::TreeServerSendPreference::DeepCopy()},
        dispatcher(), std::move(endpoints->server));

    return TreeClient{std::move(endpoints->client), dispatcher()};
  }

  TreeClient ConnectLive() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_inspect::Tree>();
    inspect::TreeServer::StartSelfManagedServer(
        inspector_,
        inspect::TreeHandlerSettings{.snapshot_behavior =
                                         inspect::TreeServerSendPreference::Live()},
        dispatcher(), std::move(endpoints->server));

    return TreeClient{std::move(endpoints->client), dispatcher()};
  }

  async::Executor executor_;

 private:
  Inspector inspector_;
};

TEST_F(InspectServiceTest, SingleTreeGetContent) {
  auto val = root().CreateInt("val", 1);
  auto client = Connect();

  fpromise::result<zx::vmo> content;
  client->GetContent().Then(
      [&](fidl::WireUnownedResult<fuchsia_inspect::Tree::GetContent>& result) {
        ZX_ASSERT_MSG(result.ok(), "Tree::GetContent failed: %s",
                      result.error().FormatDescription().c_str());
        content = fpromise::ok(std::move(result.Unwrap()->content.buffer().vmo));
      });

  RunLoopUntil([&] { return !!content; });

  auto vmo = content.take_value();
  auto hierarchy = inspect::ReadFromVmo(std::move(vmo));
  ASSERT_TRUE(hierarchy.is_ok());

  const auto* val_prop = hierarchy.value().node().get_property<inspect::IntPropertyValue>("val");
  ASSERT_NE(nullptr, val_prop);
  EXPECT_EQ(1, val_prop->value());
}

TEST_F(InspectServiceTest, SingleTreeGetContentDeepCopy) {
  auto val = root().CreateInt("val", 1);
  auto client = ConnectPrivate();

  fpromise::result<zx::vmo> content;
  client->GetContent().Then(
      [&](fidl::WireUnownedResult<fuchsia_inspect::Tree::GetContent>& result) {
        ZX_ASSERT_MSG(result.ok(), "Tree::GetContent failed: %s",
                      result.error().FormatDescription().c_str());
        content = fpromise::ok(std::move(result.Unwrap()->content.buffer().vmo));
      });

  RunLoopUntil([&] { return !!content; });

  auto vmo = content.take_value();
  auto hierarchy = inspect::ReadFromVmo(vmo);
  ASSERT_TRUE(hierarchy.is_ok());

  const auto* val_prop = hierarchy.value().node().get_property<inspect::IntPropertyValue>("val");
  ASSERT_NE(nullptr, val_prop);
  EXPECT_EQ(1, val_prop->value());

  auto should_not_see = root().CreateInt("val2", 2);
  auto hierarchy_2 = inspect::ReadFromVmo(vmo);
  ASSERT_TRUE(hierarchy_2.is_ok());

  const auto* val_prop_2 =
      hierarchy_2.value().node().get_property<inspect::IntPropertyValue>("val2");
  ASSERT_EQ(nullptr, val_prop_2);
}

TEST_F(InspectServiceTest, SingleTreeGetContentLive) {
  auto val = root().CreateInt("val", 1);
  auto client = ConnectLive();

  fpromise::result<zx::vmo> content;
  client->GetContent().Then(
      [&](fidl::WireUnownedResult<fuchsia_inspect::Tree::GetContent>& result) {
        ZX_ASSERT_MSG(result.ok(), "Tree::GetContent failed: %s",
                      result.error().FormatDescription().c_str());
        content = fpromise::ok(std::move(result.Unwrap()->content.buffer().vmo));
      });

  RunLoopUntil([&] { return !!content; });

  auto vmo = content.take_value();
  auto hierarchy = inspect::ReadFromVmo(vmo);
  ASSERT_TRUE(hierarchy.is_ok());

  const auto* val_prop = hierarchy.value().node().get_property<inspect::IntPropertyValue>("val");
  ASSERT_NE(nullptr, val_prop);
  EXPECT_EQ(1, val_prop->value());

  auto should_see = root().CreateInt("val2", 2);
  auto hierarchy_2 = inspect::ReadFromVmo(vmo);
  ASSERT_TRUE(hierarchy_2.is_ok());

  const auto* val_prop_2 =
      hierarchy_2.value().node().get_property<inspect::IntPropertyValue>("val2");
  ASSERT_NE(nullptr, val_prop_2);
  ASSERT_EQ(2, val_prop_2->value());
}

TEST_P(InspectServiceTest, ListChildNames) {
  inspect::ValueList values;
  std::vector<std::string> expected_names;
  const auto max = GetParam();
  for (auto i = 0ul; i < max; i++) {
    root().CreateLazyNode(
        "a", []() { return fpromise::make_result_promise<Inspector>(fpromise::error()); }, &values);
    expected_names.push_back(std::string("a-") + std::to_string(i));
  }

  auto client = Connect();
  auto endpoints = fidl::CreateEndpoints<fuchsia_inspect::TreeNameIterator>();

  ASSERT_TRUE(client->ListChildNames(std::move(endpoints->server)).ok());

  bool done = false;
  std::vector<std::string> names_result;
  TreeNameIteratorClient iter(std::move(endpoints->client), dispatcher());
  executor_.schedule_task(inspect::testing::ReadAllChildNames(iter).and_then(
      [&](std::vector<std::string>& promised_names) {
        names_result = std::move(promised_names);
        done = true;
      }));

  RunLoopUntil([&] { return done; });
  ASSERT_EQ(names_result.size(), max);
  std::sort(std::begin(names_result), std::end(names_result));
  std::sort(std::begin(expected_names), std::end(expected_names));
  for (size_t i = 0; i < names_result.size(); i++) {
    ASSERT_EQ(expected_names[i], names_result[i]);
  }
}

INSTANTIATE_TEST_SUITE_P(ListChildren, InspectServiceTest,
                         testing::Values(0, 20, 200, ZX_CHANNEL_MAX_MSG_BYTES));

TEST_F(InspectServiceTest, OpenChild) {
  inspect::ValueList values;
  root().CreateLazyNode(
      "a",
      []() {
        Inspector insp;
        insp.GetRoot().CreateInt("val", 1, &insp);
        return fpromise::make_ok_promise(std::move(insp));
      },
      &values);
  root().CreateLazyNode(
      "b", []() { return fpromise::make_result_promise<Inspector>(fpromise::error()); }, &values);

  auto client = Connect();
  auto iter_endpoints = fidl::CreateEndpoints<fuchsia_inspect::TreeNameIterator>();

  ASSERT_TRUE(client->ListChildNames(std::move(iter_endpoints->server)).ok());

  bool done = false;
  std::vector<std::string> names_result;
  TreeNameIteratorClient iter(std::move(iter_endpoints->client), dispatcher());
  executor_.schedule_task(inspect::testing::ReadAllChildNames(iter).and_then(
      [&](std::vector<std::string>& promised_names) {
        names_result = std::move(promised_names);
        done = true;
      }));

  RunLoopUntil([&] { return done; });
  ASSERT_EQ(names_result.size(), 2ul);

  {
    auto child_endpoints_one = fidl::CreateEndpoints<fuchsia_inspect::Tree>();
    ASSERT_TRUE(client
                    ->OpenChild(fidl::StringView::FromExternal(names_result[0]),
                                std::move(child_endpoints_one->server))
                    .ok());

    auto child_tree_client = TreeClient{std::move(child_endpoints_one->client), dispatcher()};
    fpromise::result<zx::vmo> content;
    child_tree_client->GetContent().Then(
        [&](fidl::WireUnownedResult<fuchsia_inspect::Tree::GetContent>& result) {
          ZX_ASSERT_MSG(result.ok(), "Tree::GetContent failed: %s",
                        result.error().FormatDescription().c_str());
          content = fpromise::ok(std::move(result.Unwrap()->content.buffer().vmo));
        });

    RunLoopUntil([&] { return !!content; });

    auto vmo = content.take_value();
    auto hierarchy = inspect::ReadFromVmo(std::move(vmo));
    ASSERT_TRUE(hierarchy.is_ok());

    const auto* val_prop = hierarchy.value().node().get_property<inspect::IntPropertyValue>("val");
    ASSERT_NE(nullptr, val_prop);
    EXPECT_EQ(1, val_prop->value());
  }

  {
    auto child_endpoints_one = fidl::CreateEndpoints<fuchsia_inspect::Tree>();
    ASSERT_TRUE(client
                    ->OpenChild(fidl::StringView::FromExternal(names_result[1]),
                                std::move(child_endpoints_one->server))
                    .ok());

    auto child_tree_client = TreeClient{std::move(child_endpoints_one->client), dispatcher()};
    fpromise::result<zx::vmo> content;
    child_tree_client->GetContent().Then(
        [&](fidl::WireUnownedResult<fuchsia_inspect::Tree::GetContent>& result) {
          ASSERT_FALSE(result.ok());
        });
  }
}

TEST_F(InspectServiceTest, ReadSingleLevelIntoHierarchy) {
  inspect::ValueList values;
  root().CreateLazyNode(
      "a",
      []() {
        Inspector insp;
        insp.GetRoot().CreateInt("val", 1, &insp);
        return fpromise::make_ok_promise(std::move(insp));
      },
      &values);
  root().CreateLazyNode(
      "b",
      []() {
        Inspector insp;
        insp.GetRoot().CreateInt("val", 3, &insp);
        return fpromise::make_ok_promise(std::move(insp));
      },
      &values);

  auto client = Connect();
  inspect::Hierarchy hierarchy;

  auto done = false;
  executor_.schedule_task(
      inspect::testing::ReadFromTree(client, dispatcher()).and_then([&](inspect::Hierarchy& h) {
        hierarchy = std::move(h);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  ASSERT_EQ(2ul, hierarchy.children().size());

  hierarchy.Sort();
  auto* a = hierarchy.children().at(0).node().get_property<inspect::IntPropertyValue>("val");
  ASSERT_EQ(1, a->value());
  auto* b = hierarchy.children().at(1).node().get_property<inspect::IntPropertyValue>("val");
  ASSERT_EQ(3, b->value());
}

TEST_F(InspectServiceTest, ReadMultiLevelIntoHierarchy) {
  inspect::ValueList values;
  root().CreateLazyNode(
      "a",
      []() {
        Inspector insp;
        insp.GetRoot().CreateLazyNode(
            "interior-a",
            []() {
              Inspector interior;
              interior.GetRoot().CreateInt("val", 1, &interior);
              return fpromise::make_ok_promise(std::move(interior));
            },
            &insp);

        return fpromise::make_ok_promise(std::move(insp));
      },
      &values);

  auto client = Connect();
  inspect::Hierarchy hierarchy;

  auto done = false;
  executor_.schedule_task(
      inspect::testing::ReadFromTree(client, dispatcher()).and_then([&](inspect::Hierarchy& h) {
        hierarchy = std::move(h);
        done = true;
      }));

  RunLoopUntil([&] { return done; });

  // failing because children aren't parsed yet
  ASSERT_EQ(1ul, hierarchy.children().size());
  ASSERT_EQ(1ul, hierarchy.children().at(0).children().size());
  auto* a =
      hierarchy.children().at(0).children().at(0).node().get_property<inspect::IntPropertyValue>(
          "val");
  ASSERT_EQ(1, a->value());
}

TEST_F(InspectServiceTest, ReadFromComponentInspector) {
  auto svc = component::OpenServiceRoot();
  auto client_end = component::ConnectAt<fuchsia_component::Binder>(*svc);
  ASSERT_TRUE(client_end.is_ok());

  fidl::WireSyncClient(std::move(*client_end));

  auto context = sys::ComponentContext::Create();
  fuchsia::diagnostics::ArchiveAccessorPtr accessor;
  ASSERT_EQ(ZX_OK, context->svc()->Connect(accessor.NewRequest(dispatcher())));

  inspect::contrib::ArchiveReader reader(std::move(accessor), {});

  auto result = RunPromise(reader.SnapshotInspectUntilPresent({"inspect_writer_app"}));

  auto data = result.take_value();
  uint64_t app_index;
  bool found = false;
  for (uint64_t i = 0; i < data.size(); i++) {
    if (data.at(i).component_name() == "inspect_writer_app") {
      app_index = i;
      found = true;
      break;
    }
  }

  ASSERT_TRUE(found);

  auto& app_data = data.at(app_index);

  ASSERT_EQ(1, app_data.GetByPath({"root", "val1"}).GetInt());
  ASSERT_EQ(2, app_data.GetByPath({"root", "val2"}).GetInt());
  ASSERT_EQ(3, app_data.GetByPath({"root", "val3"}).GetInt());
  ASSERT_EQ(4, app_data.GetByPath({"root", "val4"}).GetInt());
  ASSERT_EQ(0, app_data.GetByPath({"root", "child", "val"}).GetInt());
  ASSERT_EQ(
      std::string("OK"),
      std::string(app_data.GetByPath({"root", "fuchsia.inspect.Health", "status"}).GetString()));
}
}  // namespace
