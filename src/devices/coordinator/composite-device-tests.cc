// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "multiple-device-test.h"

// Reads a CreateCompositeDevice from remote, checks expectations, and sends
// a ZX_OK response.
void CheckCreateCompositeDeviceReceived(const zx::channel& remote, const char* expected_name,
                                        size_t expected_components_count,
                                        zx::channel* composite_remote_controller_coordinator,
                                        zx::channel* composite_remote_controller_controller) {
  // Read the CreateCompositeDevice request.
  FIDL_ALIGNDECL uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_bytes;
  uint32_t actual_handles;
  zx_status_t status = remote.read(0, bytes, handles, sizeof(bytes), fbl::count_of(handles),
                                   &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_LT(0, actual_bytes);
  ASSERT_EQ(2, actual_handles);
  composite_remote_controller_coordinator->reset(handles[0]);
  composite_remote_controller_controller->reset(handles[1]);

  // Validate the CreateCompositeDevice request.
  auto hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  ASSERT_EQ(fuchsia_device_manager_DevhostControllerCreateCompositeDeviceGenOrdinal, hdr->ordinal);
  status = fidl_decode(&fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequestTable,
                       bytes, actual_bytes, handles, actual_handles, nullptr);
  ASSERT_OK(status);
  auto req =
      reinterpret_cast<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceRequest*>(
          bytes);
  ASSERT_EQ(req->name.size, strlen(expected_name));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected_name),
                  reinterpret_cast<const uint8_t*>(req->name.data), req->name.size, "");
  ASSERT_EQ(expected_components_count, req->components.count);

  // Write the CreateCompositeDevice response.
  memset(bytes, 0, sizeof(bytes));
  auto resp =
      reinterpret_cast<fuchsia_device_manager_DevhostControllerCreateCompositeDeviceResponse*>(
          bytes);
  fidl_init_txn_header(&resp->hdr, 0,
                       fuchsia_device_manager_DevhostControllerCreateCompositeDeviceGenOrdinal);
  resp->status = ZX_OK;
  status =
      fidl_encode(&fuchsia_device_manager_DevhostControllerCreateCompositeDeviceResponseTable,
                  bytes, sizeof(*resp), handles, fbl::count_of(handles), &actual_handles, nullptr);
  ASSERT_OK(status);
  ASSERT_EQ(0, actual_handles);
  status = remote.write(0, bytes, sizeof(*resp), nullptr, 0);
  ASSERT_OK(status);
}

// Helper for BindComposite for issuing an AddComposite for a composite with the
// given components.  It's assumed that these components are children of
// the platform_bus and have the given protocol_id
void BindCompositeDefineComposite(const fbl::RefPtr<devmgr::Device>& platform_bus,
                                  const uint32_t* protocol_ids, size_t component_count,
                                  const zx_device_prop_t* props, size_t props_count,
                                  const char* name, zx_status_t expected_status = ZX_OK,
                                  const device_metadata_t* metadata = nullptr,
                                  size_t metadata_count = 0) {
  std::vector<llcpp::fuchsia::device::manager::DeviceComponent> components = {};
  for (size_t i = 0; i < component_count; ++i) {
    // Define a union type to avoid violating the strict aliasing rule.

    zx_bind_inst_t always = BI_MATCH();
    zx_bind_inst_t protocol = BI_MATCH_IF(EQ, BIND_PROTOCOL, protocol_ids[i]);

    llcpp::fuchsia::device::manager::DeviceComponent component;  // = &components[i];
    component.parts_count = 2;
    component.parts[0].match_program_count = 1;
    component.parts[0].match_program[0] = ::llcpp::fuchsia::device::manager::BindInstruction{
        .op = always.op,
        .arg = always.arg,
    };
    component.parts[1].match_program_count = 1;
    component.parts[1].match_program[0] = ::llcpp::fuchsia::device::manager::BindInstruction{
        .op = protocol.op,
        .arg = protocol.arg,
    };
    components.push_back(component);
  }

  auto prop_view = ::fidl::VectorView<uint64_t>(
      reinterpret_cast<uint64_t*>(const_cast<zx_device_prop_t*>(props)), props_count);

  std::vector<llcpp::fuchsia::device::manager::DeviceMetadata> metadata_list = {};
  for (size_t i = 0; i < metadata_count; i++) {
    auto meta = llcpp::fuchsia::device::manager::DeviceMetadata{
        .key = metadata[i].type,
        .data = ::fidl::VectorView(reinterpret_cast<uint8_t*>(const_cast<void*>(metadata[i].data)),
                                   metadata[i].length)};
    metadata_list.push_back(meta);
  }

  llcpp::fuchsia::device::manager::CompositeDeviceDescriptor comp_desc = {
      .props = prop_view,
      .components = ::fidl::VectorView(components),
      .coresident_device_index = 0,
      .metadata = ::fidl::VectorView(metadata_list),
  };

  devmgr::Coordinator* coordinator = platform_bus->coordinator;
  ASSERT_EQ(coordinator->AddCompositeDevice(platform_bus, name, comp_desc), expected_status);
}

class CompositeTestCase : public MultipleDeviceTestCase {
 public:
  ~CompositeTestCase() override = default;

  void CheckCompositeCreation(const char* composite_name, const size_t* device_indexes,
                              size_t device_indexes_count, size_t* component_indexes_out,
                              zx::channel* composite_remote_out1,
                              zx::channel* composite_remote_out2);

  fbl::RefPtr<devmgr::Device> GetCompositeDeviceFromComponent(const char* composite_name,
                                                              size_t component_index);

 protected:
  void SetUp() override {
    MultipleDeviceTestCase::SetUp();
    ASSERT_NOT_NULL(coordinator_.component_driver());
  }
};

fbl::RefPtr<devmgr::Device> CompositeTestCase::GetCompositeDeviceFromComponent(
    const char* composite_name, size_t component_index) {
  fbl::RefPtr<devmgr::Device> composite_device;
  auto component_device = device(component_index)->device;
  for (auto& comp : component_device->components()) {
    if (!strcmp(comp.composite()->device()->name().data(), composite_name)) {
      composite_device = comp.composite()->device();
      break;
    }
  }
  return composite_device;
}

void CompositeTestCase::CheckCompositeCreation(const char* composite_name,
                                               const size_t* device_indexes,
                                               size_t device_indexes_count,
                                               size_t* component_indexes_out,
                                               zx::channel* composite_remote_coordinator_out,
                                               zx::channel* composite_remote_controller_out) {
  for (size_t i = 0; i < device_indexes_count; ++i) {
    auto device_state = device(device_indexes[i]);
    // Check that the components got bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(
        CheckBindDriverReceived(device_state->controller_remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    char name[32];
    snprintf(name, sizeof(name), "%s-comp-device-%zu", composite_name, i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(device_state->device, name, 0, driver, &component_indexes_out[i]));
  }
  // Make sure the composite comes up
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), composite_name, device_indexes_count, composite_remote_coordinator_out,
      composite_remote_controller_out));
}

class CompositeAddOrderTestCase : public CompositeTestCase {
 public:
  enum class AddLocation {
    // Add the composite before any components
    BEFORE,
    // Add the composite after some components
    MIDDLE,
    // Add the composite after all components
    AFTER,
  };
  void ExecuteTest(AddLocation add);
};

class CompositeAddOrderSharedComponentTestCase : public CompositeAddOrderTestCase {
 public:
  enum class DevNum {
    DEV1 = 1,
    DEV2,
  };
  void ExecuteSharedComponentTest(AddLocation dev1Add, AddLocation dev2Add);
};

void CompositeAddOrderSharedComponentTestCase::ExecuteSharedComponentTest(AddLocation dev1_add,
                                                                          AddLocation dev2_add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDev1Name = "composite-dev1";
  const char* kCompositeDev2Name = "composite-dev2";
  auto do_add = [&](const char* devname) {
    ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
        platform_bus(), protocol_id, fbl::count_of(protocol_id), nullptr /* props */, 0, devname));
  };

  if (dev1_add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
  }

  if (dev2_add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
  }
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && dev1_add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
    }
    if (i == 0 && dev2_add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
    }
  }

  if (dev1_add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
  }

  zx::channel composite_remote_controller1;
  zx::channel composite_remote_controller2;
  zx::channel composite_remote_coordinator1;
  zx::channel composite_remote_coordinator2;
  size_t component_device1_indexes[fbl::count_of(device_indexes)];
  size_t component_device2_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDev1Name, device_indexes, fbl::count_of(device_indexes), component_device1_indexes,
      &composite_remote_coordinator1, &composite_remote_controller1));
  if (dev2_add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDev2Name, device_indexes, fbl::count_of(device_indexes), component_device2_indexes,
      &composite_remote_coordinator2, &composite_remote_controller2));
}

void CompositeAddOrderTestCase::ExecuteTest(AddLocation add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  auto do_add = [&]() {
    ASSERT_NO_FATAL_FAILURES(
        BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                     nullptr /* props */, 0, kCompositeDevName));
  };

  if (add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add());
    }
  }

  if (add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  zx::channel composite_remote_coordinator;
  zx::channel composite_remote_controller;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));
}
TEST_F(CompositeAddOrderTestCase, DefineBeforeDevices) {
  ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::BEFORE));
}

TEST_F(CompositeAddOrderTestCase, DefineAfterDevices) {
  ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::AFTER));
}

TEST_F(CompositeAddOrderTestCase, DefineInbetweenDevices) {
  ASSERT_NO_FATAL_FAILURES(ExecuteTest(AddLocation::MIDDLE));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1BeforeDevice2Before) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::BEFORE, AddLocation::BEFORE));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1BeforeDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::BEFORE, AddLocation::AFTER));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1MiddleDevice2Before) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::BEFORE, AddLocation::MIDDLE));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1MiddleDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::MIDDLE, AddLocation::AFTER));
}

TEST_F(CompositeAddOrderSharedComponentTestCase, DefineDevice1AfterDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedComponentTest(AddLocation::AFTER, AddLocation::AFTER));
}

TEST_F(CompositeTestCase, CantAddFromNonPlatformBus) {
  size_t index;
  ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus(), "test-device", 0, "", &index));
  auto device_state = device(index);

  uint32_t protocol_id[] = {ZX_PROTOCOL_I2C, ZX_PROTOCOL_GPIO};
  ASSERT_NO_FATAL_FAILURES(
      BindCompositeDefineComposite(device_state->device, protocol_id, fbl::count_of(protocol_id),
                                   nullptr /* props */, 0, "composite-dev", ZX_ERR_ACCESS_DENIED));
}

TEST_F(CompositeTestCase, AddMultipleSharedComponentCompositeDevices) {
  size_t device_indexes[2];
  zx_status_t status = ZX_OK;
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  for (size_t i = 1; i <= 5; i++) {
    char composite_dev_name[32];
    snprintf(composite_dev_name, sizeof(composite_dev_name), "composite-dev-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                     nullptr /* props */, 0, composite_dev_name));
  }

  zx::channel composite_remote_coordinator[5];
  zx::channel composite_remote_controller[5];
  size_t component_device_indexes[5][fbl::count_of(device_indexes)];
  for (size_t i = 1; i <= 5; i++) {
    char composite_dev_name[32];
    snprintf(composite_dev_name, sizeof(composite_dev_name), "composite-dev-%zu", i);
    ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
        composite_dev_name, device_indexes, fbl::count_of(device_indexes),
        component_device_indexes[i - 1], &composite_remote_coordinator[i - 1],
        &composite_remote_controller[i - 1]));
  }
  auto device1 = device(device_indexes[1])->device;
  size_t count = 0;
  for (auto& child : device1->children()) {
    count++;
    char name[32];
    snprintf(name, sizeof(name), "composite-dev-%zu-comp-device-1", count);
    if (strcmp(child.name().data(), name)) {
      status = ZX_ERR_INTERNAL;
    }
  }
  ASSERT_OK(status);
  ASSERT_EQ(count, 5);
}

TEST_F(CompositeTestCase, SharedComponentUnbinds) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDev1Name = "composite-dev-1";
  const char* kCompositeDev2Name = "composite-dev-2";
  ASSERT_NO_FATAL_FAILURES(
      BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                   nullptr /* props */, 0, kCompositeDev1Name));

  ASSERT_NO_FATAL_FAILURES(
      BindCompositeDefineComposite(platform_bus(), protocol_id, fbl::count_of(protocol_id),
                                   nullptr /* props */, 0, kCompositeDev2Name));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }
  zx::channel composite1_remote_controller;
  zx::channel composite2_remote_controller;
  zx::channel composite1_remote_coordinator;
  zx::channel composite2_remote_coordinator;
  size_t component_device1_indexes[fbl::count_of(device_indexes)];
  size_t component_device2_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDev1Name, device_indexes, fbl::count_of(device_indexes), component_device1_indexes,
      &composite1_remote_coordinator, &composite1_remote_controller));
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDev2Name, device_indexes, fbl::count_of(device_indexes), component_device2_indexes,
      &composite2_remote_coordinator, &composite2_remote_controller));
  coordinator_loop()->RunUntilIdle();
  {
    auto device1 = device(device_indexes[1])->device;
    fbl::RefPtr<devmgr::Device> comp_device1;
    fbl::RefPtr<devmgr::Device> comp_device2;
    for (auto& comp : device1->components()) {
      auto comp_device = comp.composite()->device();
      if (!strcmp(comp_device->name().data(), kCompositeDev1Name)) {
        comp_device1 = comp_device;
        continue;
      }
      if (!strcmp(comp_device->name().data(), kCompositeDev2Name)) {
        comp_device2 = comp_device;
        continue;
      }
    }
    ASSERT_NOT_NULL(comp_device1);
    ASSERT_NOT_NULL(comp_device2);
  }
  // Remove device 0 and its children (component and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  zx::channel& device_remote = device(device_indexes[0])->controller_remote;
  zx::channel& component1_remote = device(component_device1_indexes[0])->controller_remote;
  zx::channel& component2_remote = device(component_device2_indexes[0])->controller_remote;

  zx_txid_t txid1;
  zx_txid_t txid2;
  // Check the components have received their unbind requests.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(component1_remote, &txid1));
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceived(component2_remote, &txid2));

  // The device and composites should not have received any requests yet.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(composite1_remote_controller));
  ASSERT_FALSE(DeviceHasPendingMessages(composite2_remote_controller));

  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(component1_remote, txid1));
  ASSERT_NO_FATAL_FAILURES(SendUnbindReply(component2_remote, txid2));
  coordinator_loop()->RunUntilIdle();

  // The composites should start unbinding since the components finished unbinding.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite1_remote_controller));
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite2_remote_controller));
  coordinator_loop()->RunUntilIdle();

  // We are still waiting for the composites to be removed.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component1_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component2_remote));

  // Finish removing the composites.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite1_remote_controller));
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite2_remote_controller));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));

  // Finish removing the components.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component1_remote));
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component2_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device_remote));

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(
        CheckBindDriverReceived(device_state->controller_remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "composite-dev1-comp-device-0", 0,
                                       driver, &component_device1_indexes[0]));
  }
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(
        CheckBindDriverReceived(device_state->controller_remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "composite-dev2-comp-device-0", 0,
                                       driver, &component_device2_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDev1Name, fbl::count_of(device_indexes),
      &composite1_remote_coordinator, &composite1_remote_controller));
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDev2Name, fbl::count_of(device_indexes),
      &composite1_remote_coordinator, &composite2_remote_controller));
}

TEST_F(CompositeTestCase, ComponentUnbinds) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }
  zx::channel composite_remote_coordinator;
  zx::channel composite_remote_controller;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));
  coordinator_loop()->RunUntilIdle();

  {
    fbl::RefPtr<devmgr::Device> comp_device =
        GetCompositeDeviceFromComponent(kCompositeDevName, device_indexes[1]);
    ASSERT_NOT_NULL(comp_device);
  }

  // Remove device 0 and its children (component and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  zx::channel& device_remote = device(device_indexes[0])->controller_remote;
  zx::channel& component_remote = device(component_device_indexes[0])->controller_remote;

  // The device and composite should not have received an unbind request yet.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(composite_remote_controller));

  // Check the component and composite are unbound.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(component_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component_remote));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite_remote_controller));
  coordinator_loop()->RunUntilIdle();
  // Still waiting for the composite to be removed.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component_remote));

  // Finish removing the composite.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite_remote_controller));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));

  // Finish removing the component.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device_remote));
  coordinator_loop()->RunUntilIdle();

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(
        CheckBindDriverReceived(device_state->controller_remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "component-device-0", 0, driver,
                                       &component_device_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDevName, fbl::count_of(device_indexes),
      &composite_remote_coordinator, &composite_remote_controller));
}

TEST_F(CompositeTestCase, SuspendOrder) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  zx::channel composite_remote_coordinator;
  zx::channel composite_remote_controller;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));

  const uint32_t suspend_flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(suspend_flags));

  // Make sure none of the components have received their suspend requests
  ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_controller_remote()));
  for (auto idx : device_indexes) {
    ASSERT_FALSE(DeviceHasPendingMessages(idx));
  }
  for (auto idx : component_device_indexes) {
    ASSERT_FALSE(DeviceHasPendingMessages(idx));
  }
  // The composite should have been the first to get one
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(composite_remote_controller, suspend_flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // Next, all of the internal component devices should have them, but none of the devices
  // themselves
  ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_controller_remote()));
  for (auto idx : device_indexes) {
    ASSERT_FALSE(DeviceHasPendingMessages(idx));
  }
  for (auto idx : component_device_indexes) {
    ASSERT_NO_FATAL_FAILURES(
        CheckSuspendReceivedAndReply(device(idx)->controller_remote, suspend_flags, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Next, the devices should get them
  ASSERT_FALSE(DeviceHasPendingMessages(platform_bus_controller_remote()));
  for (auto idx : device_indexes) {
    ASSERT_NO_FATAL_FAILURES(
        CheckSuspendReceivedAndReply(device(idx)->controller_remote, suspend_flags, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Finally, the platform bus driver, which is the parent of all of the devices
  ASSERT_NO_FATAL_FAILURES(
      CheckSuspendReceivedAndReply(platform_bus_controller_remote(), suspend_flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
}

// Make sure we receive devfs notifications when composite devices appear
TEST_F(CompositeTestCase, DevfsNotifications) {
  zx::channel watcher;
  {
    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &watcher, &remote));
    ASSERT_OK(devfs_watch(coordinator()->root_device()->self, std::move(remote),
                          fuchsia_io_WATCH_MASK_ADDED));
  }

  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  zx::channel composite_remote_coordinator;
  zx::channel composite_remote_controller;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));

  uint8_t msg[fuchsia_io_MAX_FILENAME + 2];
  uint32_t msg_len = 0;
  ASSERT_OK(watcher.read(0, msg, nullptr, sizeof(msg), 0, &msg_len, nullptr));
  ASSERT_EQ(msg_len, 2 + strlen(kCompositeDevName));
  ASSERT_EQ(msg[0], fuchsia_io_WATCH_EVENT_ADDED);
  ASSERT_EQ(msg[1], strlen(kCompositeDevName));
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(kCompositeDevName), msg + 2, msg[1]);
}

// Make sure the path returned by GetTopologicalPath is accurate
TEST_F(CompositeTestCase, Topology) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus(), protocol_id,
                                                        fbl::count_of(protocol_id),
                                                        nullptr /* props */, 0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  zx::channel composite_remote_coordinator;
  zx::channel composite_remote_controller;
  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));

  devmgr::Devnode* dn = coordinator()->root_device()->self;
  fbl::RefPtr<devmgr::Device> composite_dev;
  ASSERT_OK(devmgr::devfs_walk(dn, "composite-dev", &composite_dev));

  char path_buf[PATH_MAX];
  ASSERT_OK(coordinator()->GetTopologicalPath(composite_dev, path_buf, sizeof(path_buf)));
  ASSERT_STR_EQ(path_buf, "/dev/composite-dev");
}

class CompositeMetadataTestCase : public CompositeTestCase {
 public:
  enum class AddLocation {
    // Add the composite before any components
    BEFORE,
    // Add the composite after some components
    MIDDLE,
    // Add the composite after all components
    AFTER,
  };
  static constexpr uint32_t kMetadataKey = 999;
  static constexpr char kMetadataStr[] = "composite-metadata";

  void AddCompositeDevice(AddLocation add = AddLocation::BEFORE);

  void VerifyMetadata(void* data, size_t len) {
    ASSERT_EQ(strlen(kMetadataStr) + 1, len);
    ASSERT_BYTES_EQ(data, kMetadataStr, len);
  }

  fbl::RefPtr<devmgr::Device> composite_device;

  // Hold reference to remote channels so that they do not close
  zx::channel composite_remote_coordinator;
  zx::channel composite_remote_controller;
};

void CompositeMetadataTestCase::AddCompositeDevice(AddLocation add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const device_metadata_t metadata[] = {
      {
          .type = kMetadataKey,
          .data = const_cast<char*>(kMetadataStr),
          .length = strlen(kMetadataStr) + 1,
      },
  };

  const char* kCompositeDevName = "composite-dev";
  auto do_add = [&]() {
    ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
        platform_bus(), protocol_id, fbl::count_of(protocol_id), nullptr /* props */, 0,
        kCompositeDevName, ZX_OK, metadata, countof(metadata)));
  };

  if (add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add());
    }
  }

  if (add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));
  composite_device = GetCompositeDeviceFromComponent(kCompositeDevName, device_indexes[0]);
  ASSERT_NOT_NULL(composite_device);
}

TEST_F(CompositeMetadataTestCase, AddAndGetMetadata) {
  char buf[32] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_OK(
      platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);
}

TEST_F(CompositeMetadataTestCase, FailGetMetadata) {
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_EQ(platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey + 1, nullptr, 0,
                                                     &len),
            ZX_ERR_NOT_FOUND);
}

TEST_F(CompositeMetadataTestCase, FailGetMetadataFromParent) {
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  fbl::RefPtr<devmgr::Device> parent =
      composite_device->composite()->bound_components().front().bound_device();
  ASSERT_EQ(platform_bus()->coordinator->GetMetadata(parent, kMetadataKey, nullptr, 0, &len),
            ZX_ERR_NOT_FOUND);
}

TEST_F(CompositeMetadataTestCase, DefineAfterDevices) {
  char buf[32] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice(AddLocation::AFTER));
  ASSERT_OK(
      platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);
}

TEST_F(CompositeMetadataTestCase, DefineInBetweenDevices) {
  char buf[32] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice(AddLocation::MIDDLE));
  ASSERT_OK(
      platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);
}

TEST_F(CompositeMetadataTestCase, PublishToSelf) {
  char path[256] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_OK(platform_bus()->coordinator->GetTopologicalPath(composite_device, path, 256));
  ASSERT_EQ(platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey + 1, nullptr, 0,
                                                     &len),
            ZX_ERR_NOT_FOUND);
  ASSERT_OK(platform_bus()->coordinator->PublishMetadata(composite_device, path, kMetadataKey + 1,
                                                         nullptr, 0));

  ASSERT_OK(platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey + 1, nullptr, 0,
                                                     &len));
}

TEST_F(CompositeMetadataTestCase, FailPublishToRestricted) {
  char path[256] = "/sys/";
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_NOT_OK(platform_bus()->coordinator->PublishMetadata(composite_device, path,
                                                             kMetadataKey + 1, nullptr, 0));
}

TEST_F(CompositeMetadataTestCase, GetMetadataFromChild) {
  char buf[32] = "";
  size_t len = 0;
  size_t child_index = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(composite_device, "child", ZX_PROTOCOL_AUDIO, "", &child_index));
  fbl::RefPtr<devmgr::Device> child = device(child_index)->device;
  ASSERT_OK(platform_bus()->coordinator->GetMetadata(child, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);
}

// Make sure metadata exists after composite device is destroyed and re-created
// due to component removal and addition
TEST_F(CompositeMetadataTestCase, GetMetadataAfterCompositeReassemble) {
  char buf[32] = "";
  size_t len = 0;
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };

  static_assert(fbl::count_of(protocol_id) == fbl::count_of(device_indexes));

  const device_metadata_t metadata[] = {
      {
          .type = kMetadataKey,
          .data = const_cast<char*>(kMetadataStr),
          .length = strlen(kMetadataStr) + 1,
      },
  };

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
      platform_bus(), protocol_id, fbl::count_of(protocol_id), nullptr /* props */, 0,
      kCompositeDevName, ZX_OK, metadata, countof(metadata)));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < fbl::count_of(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus(), name, protocol_id[i], "", &device_indexes[i]));
  }

  size_t component_device_indexes[fbl::count_of(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(
      kCompositeDevName, device_indexes, fbl::count_of(device_indexes), component_device_indexes,
      &composite_remote_coordinator, &composite_remote_controller));
  composite_device = GetCompositeDeviceFromComponent(kCompositeDevName, device_indexes[0]);
  ASSERT_NOT_NULL(composite_device);

  // Get and verify metadata
  ASSERT_OK(
      platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);

  // Remove device 0 and its children (component and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator_.ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  zx::channel& device_remote = device(device_indexes[0])->controller_remote;
  zx::channel& component_remote = device(component_device_indexes[0])->controller_remote;

  // The device and composite should not have received an unbind request yet.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(composite_remote_controller));

  // Check the component and composite are unbound.
  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(component_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component_remote));

  ASSERT_NO_FATAL_FAILURES(CheckUnbindReceivedAndReply(composite_remote_controller));
  coordinator_loop()->RunUntilIdle();

  // Still waiting for the composite to be removed.
  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));
  ASSERT_FALSE(DeviceHasPendingMessages(component_remote));

  // Finish removing the composite.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(composite_remote_controller));
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(DeviceHasPendingMessages(device_remote));

  // Finish removing the component.
  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(component_remote));
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(CheckRemoveReceivedAndReply(device_remote));
  coordinator_loop()->RunUntilIdle();

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus(), "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the components to get bound
    fbl::String driver = coordinator()->component_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(
        CheckBindDriverReceived(device_state->controller_remote, driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the component driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "component-device-0", 0, driver,
                                       &component_device_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      devhost_remote(), kCompositeDevName, fbl::count_of(device_indexes),
      &composite_remote_coordinator, &composite_remote_controller));

  composite_device = GetCompositeDeviceFromComponent(kCompositeDevName, device_indexes[0]);
  ASSERT_NOT_NULL(composite_device);

  // Get and verify metadata again
  ASSERT_OK(
      platform_bus()->coordinator->GetMetadata(composite_device, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);
}
