// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.devicegroup.test/cpp/wire.h>
#include <lib/driver/compat/cpp/compat.h>
#include <lib/driver2/device_group.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/driver2/service_client.h>

#include <bind/fuchsia/devicegroupbind/test/cpp/bind.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_devicegroup_test;
namespace bindlib = bind_fuchsia_devicegroupbind_test;

namespace {

// Name these differently than what the child expects, so we test that
// FDF renames these correctly.
const std::string_view kLeftName = "left-node";
const std::string_view kRightName = "right-node";
const std::string_view kOptionalName = "optional-node";

// Group 1 is created before creating both the left and right nodes.
fdf::DeviceGroup DeviceGroupOne() {
  auto bind_rules_left = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_ONE_LEFT),
  };

  auto bind_properties_left = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_LEFT),
  };

  auto bind_rules_right = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_ONE_RIGHT),
  };

  auto bind_properties_right = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_RIGHT),
  };

  auto nodes = std::vector{
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_left,
          .bind_properties = bind_properties_left,
      }},
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_right,
          .bind_properties = bind_properties_right,
      }},
  };

  return {{.topological_path = "test/path1", .nodes = nodes}};
}

// Group 2 is created after creating the right node, but before creating the left node.
fdf::DeviceGroup DeviceGroupTwo() {
  auto bind_rules_left = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_TWO_LEFT),
  };

  auto bind_properties_left = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_LEFT),
  };

  auto bind_rules_right = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_TWO_RIGHT),
  };

  auto bind_properties_right = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_RIGHT),
  };

  auto nodes = std::vector{
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_left,
          .bind_properties = bind_properties_left,
      }},
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_right,
          .bind_properties = bind_properties_right,
      }},
  };

  return {{.topological_path = "test/path2", .nodes = nodes}};
}

// Group 3 is created after creating both the left and right nodes.
fdf::DeviceGroup DeviceGroupThree() {
  auto bind_rules_left = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_THREE_LEFT),
  };

  auto bind_properties_left = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_LEFT),
  };

  auto bind_rules_right = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_THREE_RIGHT),
  };

  auto bind_properties_right = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_RIGHT),
  };

  auto nodes = std::vector{
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_left,
          .bind_properties = bind_properties_left,
      }},
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_right,
          .bind_properties = bind_properties_right,
      }},
  };

  return {{.topological_path = "test/path3", .nodes = nodes}};
}

// Group 4 is created before creating the left, optional, and right nodes.
fdf::DeviceGroup DeviceGroupFour() {
  auto bind_rules_left = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_FOUR_LEFT),
  };

  auto bind_properties_left = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_LEFT),
  };

  auto bind_rules_right = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_FOUR_RIGHT),
  };

  auto bind_properties_right = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_RIGHT),
  };

  auto bind_rules_optional = std::vector{
      driver::MakeAcceptEnumBindRule(bindlib::TEST_BIND_PROPERTY,
                                     bindlib::TEST_BIND_PROPERTY_FOUR_OPTIONAL),
  };

  auto bind_properties_optional = std::vector{
      driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY,
                               bindlib::TEST_BIND_PROPERTY_DRIVER_OPTIONAL),
  };

  auto nodes = std::vector{
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_left,
          .bind_properties = bind_properties_left,
      }},
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_right,
          .bind_properties = bind_properties_right,
      }},
      fdf::DeviceGroupNode{{
          .bind_rules = bind_rules_optional,
          .bind_properties = bind_properties_optional,
      }},
  };

  return {{.topological_path = "test/path4", .nodes = nodes}};
}

class NumberServer : public fidl::WireServer<ft::Device> {
 public:
  explicit NumberServer(uint32_t number) : number_(number) {}

  void GetNumber(GetNumberCompleter::Sync& completer) override { completer.Reply(number_); }

 private:
  uint32_t number_;
};

class RootDriver : public driver::DriverBase {
 public:
  RootDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("root", std::move(start_args), std::move(driver_dispatcher)) {}

  zx::result<> Start() override {
    node_client_.Bind(std::move(node()), dispatcher());
    // Add service "left".
    {
      component::ServiceInstanceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher(), std::move(server_end),
                                                       &this->left_server_);
      };
      zx::result<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = context().outgoing()->AddService<ft::Service>(std::move(handler), kLeftName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    // Add service "right".
    {
      component::ServiceInstanceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher(), std::move(server_end),
                                                       &this->right_server_);
      };
      zx::result<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = context().outgoing()->AddService<ft::Service>(std::move(handler), kRightName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    // Add service "optional".
    {
      component::ServiceInstanceHandler handler;
      ft::Service::Handler service(&handler);
      auto device = [this](fidl::ServerEnd<ft::Device> server_end) mutable -> void {
        fidl::BindServer<fidl::WireServer<ft::Device>>(dispatcher(), std::move(server_end),
                                                       &this->optional_server_);
      };
      zx::result<> status = service.add_device(std::move(device));
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add device %s", status.status_string());
      }
      status = context().outgoing()->AddService<ft::Service>(std::move(handler), kOptionalName);
      if (status.is_error()) {
        FDF_LOG(ERROR, "Failed to add service %s", status.status_string());
      }
    }

    // Setup the device group manager client.
    auto dgm_client = context().incoming()->Connect<fdf::DeviceGroupManager>();
    if (dgm_client.is_error()) {
      FDF_LOG(ERROR, "Failed to connect to DeviceGroupManager: %s",
              zx_status_get_string(dgm_client.error_value()));
      DropNode();
      return dgm_client.take_error();
    }

    device_group_manager_.Bind(std::move(dgm_client.value()), dispatcher());

    TestGroupOne();
    TestGroupTwo();
    TestGroupThree();
    TestGroupFour();
    return zx::ok();
  }

 private:
  // Add device group
  // Add left
  // Add right
  void TestGroupOne() {
    fit::closure add_right = [this]() {
      auto right_result = AddChild(kRightName, 1, one_right_controller_,
                                   bindlib::TEST_BIND_PROPERTY_ONE_RIGHT, []() {});
      if (!right_result) {
        FDF_LOG(ERROR, "Failed to start right child.");
        DropNode();
      }
    };

    fit::closure add_left_then_right = [this, add_right = std::move(add_right)]() mutable {
      auto left_result = AddChild(kLeftName, 1, one_left_controller_,
                                  bindlib::TEST_BIND_PROPERTY_ONE_LEFT, std::move(add_right));
      if (!left_result) {
        FDF_LOG(ERROR, "Failed to start left child.");
        DropNode();
      }
    };

    AddDeviceGroup(DeviceGroupOne(), std::move(add_left_then_right));
  }

  // Add right
  // Add device group
  // Add left
  void TestGroupTwo() {
    fit::closure add_left = [this]() mutable {
      auto left_result = AddChild(kLeftName, 2, two_left_controller_,
                                  bindlib::TEST_BIND_PROPERTY_TWO_LEFT, []() {});
      if (!left_result) {
        FDF_LOG(ERROR, "Failed to start left child.");
        DropNode();
      }
    };

    fit::closure add_device_group_then_left = [this, add_left = std::move(add_left)]() mutable {
      AddDeviceGroup(DeviceGroupTwo(), std::move(add_left));
    };

    auto right_result =
        AddChild(kRightName, 2, two_right_controller_, bindlib::TEST_BIND_PROPERTY_TWO_RIGHT,
                 std::move(add_device_group_then_left));
    if (!right_result) {
      FDF_LOG(ERROR, "Failed to start right child.");
      DropNode();
    }
  }

  // Add left
  // Add right
  // Add device group
  void TestGroupThree() {
    fit::closure add_device_group = [this]() mutable {
      AddDeviceGroup(DeviceGroupThree(), []() {});
    };

    fit::closure add_right_then_device_group = [this, add_device_group =
                                                          std::move(add_device_group)]() mutable {
      auto right_result =
          AddChild(kRightName, 3, three_right_controller_, bindlib::TEST_BIND_PROPERTY_THREE_RIGHT,
                   std::move(add_device_group));
      if (!right_result) {
        FDF_LOG(ERROR, "Failed to start right child.");
        DropNode();
      }
    };

    auto left_result =
        AddChild(kLeftName, 3, three_left_controller_, bindlib::TEST_BIND_PROPERTY_THREE_LEFT,
                 std::move(add_right_then_device_group));
    if (!left_result) {
      FDF_LOG(ERROR, "Failed to start left child.");
      DropNode();
    }
  }

  // Add device group
  // Add left
  // Add optional
  // Add right
  void TestGroupFour() {
    fit::closure add_right = [this]() {
      auto right_result = AddChild(kRightName, 4, four_right_controller_,
                                   bindlib::TEST_BIND_PROPERTY_FOUR_RIGHT, []() {});
      if (!right_result) {
        FDF_LOG(ERROR, "Failed to start right child.");
        DropNode();
      }
    };

    fit::closure add_optional_then_right = [this, add_right = std::move(add_right)]() mutable {
      auto optional_result =
          AddChild(kOptionalName, 4, four_optional_controller_,
                   bindlib::TEST_BIND_PROPERTY_FOUR_OPTIONAL, std::move(add_right));
      if (!optional_result) {
        FDF_LOG(ERROR, "Failed to start optional child.");
        DropNode();
      }
    };

    fit::closure add_left_then_optional = [this, add_optional =
                                                     std::move(add_optional_then_right)]() mutable {
      auto left_result = AddChild(kLeftName, 4, four_left_controller_,
                                  bindlib::TEST_BIND_PROPERTY_FOUR_LEFT, std::move(add_optional));
      if (!left_result) {
        FDF_LOG(ERROR, "Failed to start left child.");
        DropNode();
      }
    };

    AddDeviceGroup(DeviceGroupFour(), std::move(add_left_then_optional));
  }

  bool AddChild(std::string_view name, int group,
                fidl::SharedClient<fdf::NodeController>& controller, std::string_view property,
                fit::closure callback) {
    auto node_name = std::string(name) + "-" + std::to_string(group);
    // Set the properties of the node that a driver will bind to.
    fdf::NodeProperty node_property =
        driver::MakeEnumProperty(bindlib::TEST_BIND_PROPERTY, property);
    fdf::NodeAddArgs args({.name = node_name,
                           .offers = {{driver::MakeOffer<ft::Service>(name)}},
                           .properties = {{node_property}}});

    // Create endpoints of the `NodeController` for the node.
    auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
    if (endpoints.is_error()) {
      return false;
    }

    auto add_callback = [this, &controller, node_name, callback = std::move(callback),
                         client = std::move(endpoints->client)](
                            fidl::Result<fdf::Node::AddChild>& result) mutable {
      if (result.is_error()) {
        FDF_LOG(ERROR, "Adding child failed: %s", result.error_value().FormatDescription().c_str());
        DropNode();
        return;
      }

      controller.Bind(std::move(client), dispatcher());
      FDF_LOG(INFO, "Successfully added child %s.", node_name.c_str());
      callback();
    };

    node_client_->AddChild({std::move(args), std::move(endpoints->server), {}})
        .Then(std::move(add_callback));
    return true;
  }

  void AddDeviceGroup(fdf::DeviceGroup dev_group, fit::closure callback) {
    auto dev_group_name = dev_group.topological_path();
    device_group_manager_->CreateDeviceGroup(std::move(dev_group))
        .Then([this, dev_group_name, callback = std::move(callback)](
                  fidl::Result<fdf::DeviceGroupManager::CreateDeviceGroup>& create_result) {
          if (create_result.is_error()) {
            FDF_LOG(ERROR, "CreateDeviceGroup failed: %s",
                    create_result.error_value().FormatDescription().c_str());
            DropNode();
            return;
          }

          auto name = dev_group_name.has_value() ? dev_group_name.value() : "";
          FDF_LOG(INFO, "Succeeded adding device group %s.", name.c_str());
          callback();
        });
  }

  void DropNode() { node_client_.AsyncTeardown(); }

  fidl::SharedClient<fdf::NodeController> one_left_controller_;
  fidl::SharedClient<fdf::NodeController> one_right_controller_;

  fidl::SharedClient<fdf::NodeController> two_left_controller_;
  fidl::SharedClient<fdf::NodeController> two_right_controller_;

  fidl::SharedClient<fdf::NodeController> three_left_controller_;
  fidl::SharedClient<fdf::NodeController> three_right_controller_;

  fidl::SharedClient<fdf::NodeController> four_left_controller_;
  fidl::SharedClient<fdf::NodeController> four_right_controller_;
  fidl::SharedClient<fdf::NodeController> four_optional_controller_;

  fidl::SharedClient<fdf::Node> node_client_;
  fidl::SharedClient<fdf::DeviceGroupManager> device_group_manager_;

  NumberServer left_server_ = NumberServer(1);
  NumberServer right_server_ = NumberServer(2);
  NumberServer optional_server_ = NumberServer(3);
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V3(driver::Record<RootDriver>);
