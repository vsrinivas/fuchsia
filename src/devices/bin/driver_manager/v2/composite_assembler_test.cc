// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/composite_assembler.h"

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

constexpr uint32_t kPropId = 2;
constexpr uint32_t kPropValue = 10;
constexpr std::string_view kCompositeName = "device-1";
constexpr std::string_view kCompositeName2 = "device-2";
constexpr std::string_view kFragmentName = "child-1";
constexpr std::string_view kFragmentName2 = "child-2";

class CompositeAssemblerTest : public gtest::TestLoopFixture {};

class TestBinder : public dfv2::DriverBinder {
 public:
  explicit TestBinder(fit::function<void(dfv2::Node&)> cb) : callback(std::move(cb)) {}

  fit::function<void(dfv2::Node&)> callback;

  void Bind(dfv2::Node& node, std::shared_ptr<dfv2::BindResultTracker> result_tracker) override {
    callback(node);
  }
};

TEST_F(CompositeAssemblerTest, EmptyManager) {
  TestBinder binder([](auto& node) {});
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, NoMatches) {
  TestBinder binder([](auto& node) {});
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());

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
  TestBinder binder([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());

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
  TestBinder binder([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());

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

  ASSERT_EQ(1ul, child->properties().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Check that our node no longer matches now that the composite has been created.
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, CreateTwoParentComposite) {
  bool bind_was_called = false;
  TestBinder binder([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  auto node2 =
      std::make_shared<dfv2::Node>("parent2", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());

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

  ASSERT_EQ(1ul, child->properties().size());
  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Check that our node no longer matches now that the composite has been created.
  ASSERT_FALSE(manager.BindNode(node));
}

TEST_F(CompositeAssemblerTest, NodeRemovesCorrectly) {
  bool bind_was_called = false;
  TestBinder binder([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  auto node2 =
      std::make_shared<dfv2::Node>("parent2", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());

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
  node = std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
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
  TestBinder binder([&bind_was_called](auto& node) { bind_was_called = true; });
  auto node =
      std::make_shared<dfv2::Node>("parent", std::vector<dfv2::Node*>(), &binder, dispatcher());
  dfv2::CompositeDeviceManager manager(&binder, dispatcher());

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

  // Check that we created a child node.
  ASSERT_TRUE(bind_was_called);
  ASSERT_EQ(1ul, node->children().size());
  auto child = node->children().front();
  ASSERT_EQ(kCompositeName, child->name());
  ASSERT_EQ(1ul, child->parents().size());

  ASSERT_EQ(kPropId, child->properties()[0].key().int_value());
  ASSERT_EQ(kPropValue, child->properties()[0].value().int_value());

  // Match to the second composite device.
  ASSERT_TRUE(manager.BindNode(node));
  ASSERT_EQ(2ul, node->children().size());
  child = node->children().back();
  ASSERT_EQ(kCompositeName2, child->name());
  ASSERT_EQ(1ul, child->parents().size());

  // Check that our node no longer matches now that both composites have been created.
  ASSERT_FALSE(manager.BindNode(node));
}
