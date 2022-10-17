// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/composite_assembler.h"

#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

constexpr uint32_t kPropId = 2;
constexpr uint32_t kPropValue = 10;
constexpr std::string_view kCompositeName = "device-1";
constexpr std::string_view kCompositeName2 = "device-2";
constexpr std::string_view kFragmentName = "child-1";
constexpr std::string_view kFragmentName2 = "child-2";

class CompositeAssemblerTest : public gtest::TestLoopFixture {};

class TestNodeManager : public dfv2::NodeManager {
 public:
  explicit TestNodeManager(fit::function<void(dfv2::Node&)> cb) : callback(std::move(cb)) {}

  fit::function<void(dfv2::Node&)> callback;

  void Bind(dfv2::Node& node, std::shared_ptr<dfv2::BindResultTracker> result_tracker) override {
    callback(node);
  }
  zx::result<dfv2::DriverHost*> CreateDriverHost() override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
};

TEST_F(CompositeAssemblerTest, EmptyManager) {
  TestNodeManager node_manager([](auto& node) {});
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, NoMatches) {
  TestNodeManager node_manager([](auto& node) {});
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = "child";
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_ABORT();

  descriptor.fragments().push_back(std::move(fragment));
  manager.AddCompositeDevice("device-1", descriptor);
  ASSERT_FALSE(manager.BindNode(node));
}

// Check that matching just one fragment out of multiple works as expected.
TEST_F(CompositeAssemblerTest, MatchButDontCreate) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();

  // Create two fragments
  descriptor.fragments().push_back(fragment);
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);
  ASSERT_TRUE(manager.BindNode(node));

  // Check that we did not create a second node.
  ASSERT_FALSE(bind_was_called);
  ASSERT_EQ(0ul, node->children().size());
}

// Create a one-node composite.
TEST_F(CompositeAssemblerTest, CreateSingleParentComposite) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);

  ASSERT_TRUE(manager.BindNode(node));

  // Check that we created a child node.
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(1ul, node->children().size());
  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(1ul, child->parents().size());

  ASSERT_EQ(2ul, child->properties().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  ASSERT_EQ(static_cast<uint32_t>(BIND_COMPOSITE), child->properties()[1].key().int_value());
  ASSERT_EQ(1ul, child->properties()[1].value().int_value());

  // Check that our node no longer matches now that the composite has been created.
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, CreateTwoParentComposite) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  auto node2 = std::make_shared<dfv2::Node>("parent2", std::vector<dfv2::Node*>(), &node_manager,
                                            dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  // Create two fragments
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  fragment.name() = kFragmentName2;
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);

  ASSERT_TRUE(manager.BindNode(node));
  ASSERT_TRUE(manager.BindNode(node2));

  // Check that we created a child node.
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(1ul, node->children().size());
  ASSERT_EQ(1ul, node2->children().size());
  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(2ul, child->parents().size());

  ASSERT_EQ(2ul, child->properties().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  ASSERT_EQ(static_cast<uint32_t>(BIND_COMPOSITE), child->properties()[1].key().int_value());
  ASSERT_EQ(1ul, child->properties()[1].value().int_value());

  // Check that our node no longer matches now that the composite has been created.
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, NodeRemovesCorrectly) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  auto node2 = std::make_shared<dfv2::Node>("parent2", std::vector<dfv2::Node*>(), &node_manager,
                                            dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  // Create two fragments
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  fragment.name() = kFragmentName2;
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);

  // Bind the first node, then reset it, then rebind it.
  ASSERT_TRUE(manager.BindNode(node));
  node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                      dispatcher());
  ASSERT_TRUE(manager.BindNode(node));
  ASSERT_EQ(0ul, node->children().size());

  // Now bind the second node and check that the composite is created.
  ASSERT_TRUE(manager.BindNode(node2));

  // Check that we created a child node.
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(1ul, node->children().size());
  ASSERT_EQ(1ul, node2->children().size());
  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(2ul, child->parents().size());

  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Check that our node no longer matches now that the composite has been created.
  ASSERT_FALSE(manager.BindNode(node));
}

// Check that having two composite devices that both bind to the same node works.
TEST_F(CompositeAssemblerTest, TwoSingleParentComposite) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  // Create two composite device assemblers.
  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);
  manager.AddCompositeDevice(std::string(kCompositeName2), descriptor);

  ASSERT_TRUE(manager.BindNode(node));

  // Check that both parents were created.
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(2ul, node->children().size());

  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(1ul, child->parents().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Match to the second composite device.
  child = node->children().back();
  ASSERT_EQ(kCompositeName2, child->name());
  ASSERT_EQ(1ul, child->parents().size());

  // Check that our node no longer matches now that both composites have been created.
  ASSERT_FALSE(manager.BindNode(node));
}

// Check that adding a composite after a node has already matched works.
TEST_F(CompositeAssemblerTest, AddCompositeAfterNode) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  // Create two composite device assemblers.
  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);

  // Bind our node.
  ASSERT_TRUE(manager.BindNode(node));
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(1ul, node->children().size());

  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(1ul, child->parents().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Add a new composite device. The Manager should check already bound nodes,
  // so it should create a new composite device immediately,
  manager.AddCompositeDevice(std::string(kCompositeName2), descriptor);

  ASSERT_EQ(2ul, node->children().size());
  child = node->children().back();
  ASSERT_EQ(kCompositeName2, child->name());
  ASSERT_EQ(1ul, child->parents().size());

  // Check that our node no longer matches now that both composites have been created.
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, Rebind) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);

  // Bind our node.
  ASSERT_TRUE(manager.BindNode(node));
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(1ul, node->children().size());

  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(1ul, child->parents().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Try and rebind. This shouldn't change anything
  manager.RebindNodes();

  // Add a new composite device. The Manager should check already bound nodes,
  // so it should create a new composite device immediately,
  manager.AddCompositeDevice(std::string(kCompositeName2), descriptor);

  ASSERT_EQ(2ul, node->children().size());
  child = node->children().back();
  ASSERT_EQ(kCompositeName2, child->name());
  ASSERT_EQ(1ul, child->parents().size());

  // Check that our node no longer matches now that both composites have been created.
  ASSERT_FALSE(manager.BindNode(node));
}

class FakeContext : public fpromise::context {
 public:
  fpromise::executor* executor() const override {
    EXPECT_TRUE(false);
    return nullptr;
  }

  fpromise::suspended_task suspend_task() override {
    EXPECT_TRUE(false);
    return fpromise::suspended_task();
  }
};

TEST_F(CompositeAssemblerTest, InspectNodes) {
  bool bind_was_called = false;
  TestNodeManager node_manager([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &node_manager,
                                           dispatcher());
  dfv2::CompositeDeviceManager manager(&node_manager, dispatcher(), []() {});

  fuchsia_device_manager::CompositeDeviceDescriptor descriptor;
  fuchsia_device_manager::DeviceFragment fragment;
  fragment.name() = kFragmentName;
  fragment.parts().emplace_back();
  fragment.parts()[0].match_program().emplace_back();
  fragment.parts()[0].match_program()[0] = fuchsia_device_manager::BindInstruction BI_MATCH();
  descriptor.fragments().push_back(fragment);

  descriptor.props().emplace_back();
  descriptor.props()[0].id() = kPropId;
  descriptor.props()[0].value() = kPropValue;

  // Create two composite device assemblers.
  manager.AddCompositeDevice(std::string(kCompositeName), descriptor);

  FakeContext context;

  // Check the inspect data with an unbound node.
  {
    inspect::Inspector inspector;
    manager.Inspect(inspector.GetRoot());
    auto heirarchy = inspect::ReadFromInspector(inspector)(context).take_value();
    ASSERT_EQ(1ul, heirarchy.children().size());
    auto& assembler = heirarchy.children()[0];
    EXPECT_EQ(std::string("assembler-0x0"), heirarchy.children()[0].node().name());
    EXPECT_EQ(
        std::string(kCompositeName),
        heirarchy.children()[0].node().get_property<inspect::StringPropertyValue>("name")->value());
    auto inspect_fragment =
        assembler.node().get_property<inspect::StringPropertyValue>(std::string(kFragmentName));
    ASSERT_NE(inspect_fragment, nullptr);
    EXPECT_EQ(std::string("<unbound>"), inspect_fragment->value());
  }

  ASSERT_TRUE(manager.BindNode(node));

  // Check the inspect data with a bound node.
  {
    inspect::Inspector inspector;
    manager.Inspect(inspector.GetRoot());
    auto heirarchy = inspect::ReadFromInspector(inspector)(context).take_value();
    ASSERT_EQ(1ul, heirarchy.children().size());
    auto& assembler = heirarchy.children()[0];
    EXPECT_EQ(std::string("assembler-0x0"), heirarchy.children()[0].node().name());
    EXPECT_EQ(
        std::string(kCompositeName),
        heirarchy.children()[0].node().get_property<inspect::StringPropertyValue>("name")->value());
    auto inspect_fragment =
        assembler.node().get_property<inspect::StringPropertyValue>(std::string(kFragmentName));
    ASSERT_NE(inspect_fragment, nullptr);
    EXPECT_EQ(std::string("parent"), inspect_fragment->value());
  }
}
