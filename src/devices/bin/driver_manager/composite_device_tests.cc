// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/driver.h>

#include <vector>

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "multiple_device_test.h"
#include "src/devices/lib/log/log.h"

namespace fio = fuchsia_io;

namespace {

class FidlTransaction : public fidl::Transaction {
 public:
  FidlTransaction(FidlTransaction&&) = default;
  explicit FidlTransaction(zx_txid_t transaction_id, zx::unowned_channel channel)
      : txid_(transaction_id), channel_(channel) {}

  std::unique_ptr<fidl::Transaction> TakeOwnership() override {
    return std::make_unique<FidlTransaction>(std::move(*this));
  }

  zx_status_t Reply(fidl::OutgoingMessage* message) override {
    ZX_ASSERT(txid_ != 0);
    message->set_txid(txid_);
    txid_ = 0;
    message->Write(channel_);
    return message->status();
  }

  void Close(zx_status_t epitaph) override { ZX_ASSERT(false); }

  void InternalError(fidl::UnbindInfo info, fidl::ErrorOrigin origin) override {
    detected_error_ = info;
  }

  ~FidlTransaction() override = default;

  const std::optional<fidl::UnbindInfo>& detected_error() const { return detected_error_; }

 private:
  zx_txid_t txid_;
  zx::unowned_channel channel_;
  std::optional<fidl::UnbindInfo> detected_error_;
};

class FakeDevhost : public fidl::WireServer<fuchsia_device_manager::DriverHostController> {
 public:
  FakeDevhost(const char* expected_name, size_t expected_fragments_count,
              fidl::ClientEnd<fuchsia_device_manager::Coordinator>* device_coordinator_client,
              fidl::ServerEnd<fuchsia_device_manager::DeviceController>* device_controller_server)
      : expected_name_(expected_name),
        expected_fragments_count_(expected_fragments_count),
        device_coordinator_client_(device_coordinator_client),
        device_controller_server_(device_controller_server) {}

  void CreateDevice(CreateDeviceRequestView request,
                    CreateDeviceCompleter::Sync& completer) override {
    if (request->type.is_composite()) {
      auto& composite = request->type.composite();
      if (strncmp(expected_name_, composite.name.data(), composite.name.size()) == 0 &&
          composite.fragments.count() == expected_fragments_count_) {
        *device_coordinator_client_ = std::move(request->coordinator);
        *device_controller_server_ = std::move(request->device_controller);
        completer.Reply(ZX_OK);
        return;
      }
      completer.Reply(ZX_ERR_INTERNAL);
    }
  }

  void Restart(RestartRequestView request, RestartCompleter::Sync& completer) override {}

 private:
  const char* expected_name_;
  size_t expected_fragments_count_;
  fidl::ClientEnd<fuchsia_device_manager::Coordinator>* device_coordinator_client_;
  fidl::ServerEnd<fuchsia_device_manager::DeviceController>* device_controller_server_;
};

}  // namespace

// Reads a CreateCompositeDevice from remote, checks expectations, and sends
// a ZX_OK response.
void CheckCreateCompositeDeviceReceived(
    const fidl::ServerEnd<fdm::DriverHostController>& controller, const char* expected_name,
    size_t expected_fragments_count, DeviceState* composite) {
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::IncomingMessage msg =
      fidl::ChannelReadEtc(controller.channel().get(), 0, fidl::BufferSpan(bytes, std::size(bytes)),
                           cpp20::span(handles));
  ASSERT_TRUE(msg.ok());

  auto* header = msg.header();
  FidlTransaction txn(header->txid, zx::unowned(controller.channel()));

  FakeDevhost fake(expected_name, expected_fragments_count, &composite->coordinator_client,
                   &composite->controller_server);
  fidl::WireDispatch(
      static_cast<fidl::WireServer<fuchsia_device_manager::DriverHostController>*>(&fake),
      std::move(msg), &txn);
  ASSERT_FALSE(txn.detected_error());
  ASSERT_TRUE(composite->coordinator_client.is_valid());
  ASSERT_TRUE(composite->controller_server.is_valid());
}

// Helper for BindComposite for issuing an AddComposite for a composite with the
// given fragments.  It's assumed that these fragments are children of
// the platform_bus and have the given protocol_id
void BindCompositeDefineComposite(const fbl::RefPtr<Device>& platform_bus,
                                  const uint32_t* protocol_ids, size_t fragment_count,
                                  const zx_device_prop_t* props, size_t props_count,
                                  const char* name, zx_status_t expected_status = ZX_OK,
                                  const device_metadata_t* metadata = nullptr,
                                  size_t metadata_count = 0) {
  fidl::Arena allocator;
  std::vector<fuchsia_device_manager::wire::DeviceFragment> fragments = {};
  for (size_t i = 0; i < fragment_count; ++i) {
    // Define a union type to avoid violating the strict aliasing rule.

    zx_bind_inst_t always = BI_MATCH();
    zx_bind_inst_t protocol = BI_MATCH_IF(EQ, BIND_PROTOCOL, protocol_ids[i]);

    fuchsia_device_manager::wire::DeviceFragment fragment;  // = &fragments[i];
    fragment.name = ::fidl::StringView("unnamed-fragment");
    fragment.parts.Allocate(allocator, 1);
    fragment.parts[0].match_program.Allocate(allocator, 1);
    fragment.parts[0].match_program[0] = fuchsia_device_manager::wire::BindInstruction{
        .op = protocol.op,
        .arg = protocol.arg,
        .debug = always.debug,
    };
    fragments.push_back(std::move(fragment));
  }

  std::vector<fuchsia_device_manager::wire::DeviceProperty> props_list = {};
  for (size_t i = 0; i < props_count; i++) {
    props_list.push_back(fuchsia_device_manager::wire::DeviceProperty{
        .id = props[i].id,
        .reserved = props[i].reserved,
        .value = props[i].value,
    });
  }

  std::vector<fuchsia_device_manager::wire::DeviceMetadata> metadata_list = {};
  for (size_t i = 0; i < metadata_count; i++) {
    auto meta = fuchsia_device_manager::wire::DeviceMetadata{
        .key = metadata[i].type,
        .data = ::fidl::VectorView<uint8_t>::FromExternal(
            reinterpret_cast<uint8_t*>(const_cast<void*>(metadata[i].data)), metadata[i].length)};
    metadata_list.emplace_back(std::move(meta));
  }

  fuchsia_device_manager::wire::CompositeDeviceDescriptor comp_desc = {
      .props = ::fidl::VectorView<fuchsia_device_manager::wire::DeviceProperty>::FromExternal(
          props_list),
      .fragments =
          ::fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment>::FromExternal(fragments),
      .primary_fragment_index = 0,
      .spawn_colocated = true,
      .metadata = ::fidl::VectorView<fuchsia_device_manager::wire::DeviceMetadata>::FromExternal(
          metadata_list),
  };

  Coordinator* coordinator = platform_bus->coordinator;
  ASSERT_EQ(coordinator->AddCompositeDevice(platform_bus, name, std::move(comp_desc)),
            expected_status);
}

class CompositeTestCase : public MultipleDeviceTestCase {
 public:
  ~CompositeTestCase() override = default;

  void CheckCompositeCreation(const char* composite_name, const size_t* device_indexes,
                              size_t device_indexes_count, size_t* fragment_indexes_out,
                              DeviceState* composite_state);

  fbl::RefPtr<Device> GetCompositeDeviceFromFragment(const char* composite_name,
                                                     size_t fragment_index);

 protected:
  void SetUp() override {
    MultipleDeviceTestCase::SetUp();
    ASSERT_NOT_NULL(coordinator().fragment_driver());
  }
};

fbl::RefPtr<Device> CompositeTestCase::GetCompositeDeviceFromFragment(const char* composite_name,
                                                                      size_t fragment_index) {
  fbl::RefPtr<Device> composite_device;
  auto fragment_device = device(fragment_index)->device;
  for (auto& comp : fragment_device->fragments()) {
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
                                               size_t* fragment_indexes_out,
                                               DeviceState* composite) {
  for (size_t i = 0; i < device_indexes_count; ++i) {
    auto device_state = device(device_indexes[i]);
    // Check that the fragments got bound
    fbl::String driver = coordinator().fragment_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(device_state->CheckBindDriverReceivedAndReply(driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the fragment driver would send
    char name[32];
    snprintf(name, sizeof(name), "%s-comp-device-%zu", composite_name, i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(device_state->device, name, 0, driver, &fragment_indexes_out[i]));
  }
  // Make sure the composite comes up
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(driver_host_server(), composite_name,
                                                              device_indexes_count, composite));
}

class CompositeAddOrderTestCase : public CompositeTestCase {
 public:
  enum class AddLocation {
    // Add the composite before any fragments
    BEFORE,
    // Add the composite after some fragments
    MIDDLE,
    // Add the composite after all fragments
    AFTER,
  };
  void ExecuteTest(AddLocation add);
};

class CompositeAddOrderSharedFragmentTestCase : public CompositeAddOrderTestCase {
 public:
  enum class DevNum {
    DEV1 = 1,
    DEV2,
  };
  void ExecuteSharedFragmentTest(AddLocation dev1Add, AddLocation dev2Add);
};

void CompositeAddOrderSharedFragmentTestCase::ExecuteSharedFragmentTest(AddLocation dev1_add,
                                                                        AddLocation dev2_add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDev1Name = "composite-dev1";
  const char* kCompositeDev2Name = "composite-dev2";
  auto do_add = [&](const char* devname) {
    ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                          std::size(protocol_id),
                                                          nullptr /* props */, 0, devname));
  };

  if (dev1_add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev1Name));
  }

  if (dev2_add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
  }
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
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

  DeviceState composite1, composite2;
  size_t fragment_device1_indexes[std::size(device_indexes)];
  size_t fragment_device2_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev1Name, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device1_indexes, &composite1));
  if (dev2_add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add(kCompositeDev2Name));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev2Name, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device2_indexes, &composite2));
}

void CompositeAddOrderTestCase::ExecuteTest(AddLocation add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  auto do_add = [&]() {
    ASSERT_NO_FATAL_FAILURES(
        BindCompositeDefineComposite(platform_bus()->device, protocol_id, std::size(protocol_id),
                                     nullptr /* props */, 0, kCompositeDevName));
  };

  if (add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add());
    }
  }

  if (add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  DeviceState composite;
  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));
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

TEST_F(CompositeAddOrderSharedFragmentTestCase, DefineDevice1BeforeDevice2Before) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedFragmentTest(AddLocation::BEFORE, AddLocation::BEFORE));
}

TEST_F(CompositeAddOrderSharedFragmentTestCase, DefineDevice1BeforeDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedFragmentTest(AddLocation::BEFORE, AddLocation::AFTER));
}

TEST_F(CompositeAddOrderSharedFragmentTestCase, DefineDevice1MiddleDevice2Before) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedFragmentTest(AddLocation::BEFORE, AddLocation::MIDDLE));
}

TEST_F(CompositeAddOrderSharedFragmentTestCase, DefineDevice1MiddleDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedFragmentTest(AddLocation::MIDDLE, AddLocation::AFTER));
}

TEST_F(CompositeAddOrderSharedFragmentTestCase, DefineDevice1AfterDevice2After) {
  ASSERT_NO_FATAL_FAILURES(ExecuteSharedFragmentTest(AddLocation::AFTER, AddLocation::AFTER));
}

TEST_F(CompositeTestCase, AddMultipleSharedFragmentCompositeDevices) {
  size_t device_indexes[2];
  zx_status_t status = ZX_OK;
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  for (size_t i = 1; i <= 5; i++) {
    char composite_dev_name[32];
    snprintf(composite_dev_name, sizeof(composite_dev_name), "composite-dev-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        BindCompositeDefineComposite(platform_bus()->device, protocol_id, std::size(protocol_id),
                                     nullptr /* props */, 0, composite_dev_name));
  }

  DeviceState composite[5];
  size_t fragment_device_indexes[5][std::size(device_indexes)];
  for (size_t i = 1; i <= 5; i++) {
    char composite_dev_name[32];
    snprintf(composite_dev_name, sizeof(composite_dev_name), "composite-dev-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        CheckCompositeCreation(composite_dev_name, device_indexes, std::size(device_indexes),
                               fragment_device_indexes[i - 1], &composite[i - 1]));
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

TEST_F(CompositeTestCase, SharedFragmentUnbinds) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDev1Name = "composite-dev-1";
  const char* kCompositeDev2Name = "composite-dev-2";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDev1Name));

  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDev2Name));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  DeviceState composite1, composite2;
  size_t fragment_device1_indexes[std::size(device_indexes)];
  size_t fragment_device2_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev1Name, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device1_indexes, &composite1));
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDev2Name, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device2_indexes, &composite2));
  coordinator_loop()->RunUntilIdle();
  {
    auto device1 = device(device_indexes[1])->device;
    fbl::RefPtr<Device> comp_device1;
    fbl::RefPtr<Device> comp_device2;
    for (auto& comp : device1->fragments()) {
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
  // Remove device 0 and its children (fragment and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  auto device_zero = device(device_indexes[0]);
  auto fragment1 = device(fragment_device1_indexes[0]);
  auto fragment2 = device(fragment_device2_indexes[0]);

  // Check the fragments have received their unbind requests.
  ASSERT_NO_FATAL_FAILURES(fragment1->CheckUnbindReceived());
  ASSERT_NO_FATAL_FAILURES(fragment2->CheckUnbindReceived());

  // The device and composites should not have received any requests yet.
  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(composite1.HasPendingMessages());
  ASSERT_FALSE(composite2.HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(fragment1->SendUnbindReply());
  ASSERT_NO_FATAL_FAILURES(fragment2->SendUnbindReply());
  coordinator_loop()->RunUntilIdle();

  // The composites should start unbinding since the fragments finished unbinding.
  ASSERT_NO_FATAL_FAILURES(composite1.CheckUnbindReceivedAndReply());
  ASSERT_NO_FATAL_FAILURES(composite2.CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // We are still waiting for the composites to be removed.
  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(fragment1->HasPendingMessages());
  ASSERT_FALSE(fragment2->HasPendingMessages());

  // Finish removing the composites.
  ASSERT_NO_FATAL_FAILURES(composite1.CheckRemoveReceivedAndReply());
  ASSERT_NO_FATAL_FAILURES(composite2.CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(device_zero->HasPendingMessages());

  // Finish removing the fragments.
  ASSERT_NO_FATAL_FAILURES(fragment1->CheckRemoveReceivedAndReply());
  ASSERT_NO_FATAL_FAILURES(fragment2->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(device_zero->CheckRemoveReceivedAndReply());

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the fragments to get bound
    fbl::String driver = coordinator().fragment_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(device_state->CheckBindDriverReceivedAndReply(driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the fragment driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "composite-dev1-comp-device-0", 0,
                                       driver, &fragment_device1_indexes[0]));
  }
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the fragments to get bound
    fbl::String driver = coordinator().fragment_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(device_state->CheckBindDriverReceivedAndReply(driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the fragment driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "composite-dev2-comp-device-0", 0,
                                       driver, &fragment_device2_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      driver_host_server(), kCompositeDev1Name, std::size(device_indexes), &composite1));
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      driver_host_server(), kCompositeDev2Name, std::size(device_indexes), &composite2));
}

TEST_F(CompositeTestCase, FragmentUnbinds) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDevName));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }
  DeviceState composite;
  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));
  coordinator_loop()->RunUntilIdle();

  {
    fbl::RefPtr<Device> comp_device =
        GetCompositeDeviceFromFragment(kCompositeDevName, device_indexes[1]);
    ASSERT_NOT_NULL(comp_device);
  }

  // Remove device 0 and its children (fragment and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  auto device_zero = device(device_indexes[0]);
  auto fragment = device(fragment_device_indexes[0]);

  // The device and composite should not have received an unbind request yet.
  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(composite.HasPendingMessages());

  // Check the fragment and composite are unbound.
  ASSERT_NO_FATAL_FAILURES(fragment->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(fragment->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(composite.CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();
  // Still waiting for the composite to be removed.
  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(fragment->HasPendingMessages());

  // Finish removing the composite.
  ASSERT_NO_FATAL_FAILURES(composite.CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(device_zero->HasPendingMessages());

  // Finish removing the fragment.
  ASSERT_NO_FATAL_FAILURES(fragment->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(device_zero->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the fragments to get bound
    fbl::String driver = coordinator().fragment_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(device_state->CheckBindDriverReceivedAndReply(driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the fragment driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "fragment-device-0", 0, driver,
                                       &fragment_device_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      driver_host_server(), kCompositeDevName, std::size(device_indexes), &composite));
}

TEST_F(CompositeTestCase, SuspendOrder) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  DeviceState composite;
  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));

  const uint32_t suspend_flags = DEVICE_SUSPEND_FLAG_POWEROFF;
  ASSERT_NO_FATAL_FAILURES(DoSuspend(suspend_flags));

  // Make sure none of the fragments have received their suspend requests
  ASSERT_FALSE(platform_bus()->HasPendingMessages());
  for (auto idx : device_indexes) {
    ASSERT_FALSE(device(idx)->HasPendingMessages());
  }
  for (auto idx : fragment_device_indexes) {
    ASSERT_FALSE(device(idx)->HasPendingMessages());
  }
  // The composite should have been the first to get one
  ASSERT_NO_FATAL_FAILURES(composite.CheckSuspendReceivedAndReply(suspend_flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // Next, all of the internal fragment devices should have them, but none of the devices
  // themselves
  ASSERT_FALSE(platform_bus()->HasPendingMessages());
  for (auto idx : device_indexes) {
    ASSERT_FALSE(device(idx)->HasPendingMessages());
  }
  for (auto idx : fragment_device_indexes) {
    ASSERT_NO_FATAL_FAILURES(device(idx)->CheckSuspendReceivedAndReply(suspend_flags, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Next, the devices should get them
  ASSERT_FALSE(platform_bus()->HasPendingMessages());
  for (auto idx : device_indexes) {
    ASSERT_NO_FATAL_FAILURES(device(idx)->CheckSuspendReceivedAndReply(suspend_flags, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Finally, the platform bus driver, which is the parent of all of the devices
  ASSERT_NO_FATAL_FAILURES(platform_bus()->CheckSuspendReceivedAndReply(suspend_flags, ZX_OK));
  coordinator_loop()->RunUntilIdle();
}

TEST_F(CompositeTestCase, ResumeOrder) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  size_t fragment_device_indexes[std::size(device_indexes)];
  DeviceState composite;
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));
  fbl::RefPtr<Device> comp_device =
      GetCompositeDeviceFromFragment(kCompositeDevName, device_indexes[1]);
  ASSERT_NOT_NULL(comp_device);

  // Put all the devices in suspended state
  coordinator().sys_device()->set_state(Device::State::kSuspended);
  coordinator().sys_device()->proxy()->set_state(Device::State::kSuspended);
  platform_bus()->device->set_state(Device::State::kSuspended);
  for (auto idx : device_indexes) {
    device(idx)->device->set_state(Device::State::kSuspended);
  }
  for (auto idx : fragment_device_indexes) {
    device(idx)->device->set_state(Device::State::kSuspended);
  }
  comp_device->set_state(Device::State::kSuspended);

  fuchsia_hardware_power_statecontrol::wire::SystemPowerState state =
      fuchsia_hardware_power_statecontrol::wire::SystemPowerState::kFullyOn;
  ASSERT_NO_FATAL_FAILURES(DoResume(state));

  // First, the sys proxy driver, which is the parent of all of the devices
  ASSERT_NO_FATAL_FAILURES(
      sys_proxy()->CheckResumeReceivedAndReply(SystemPowerState::kFullyOn, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // Then platform devices
  ASSERT_NO_FATAL_FAILURES(platform_bus()->CheckResumeReceivedAndReply(state, ZX_OK));
  coordinator_loop()->RunUntilIdle();

  // Next the devices
  for (auto idx : device_indexes) {
    ASSERT_NO_FATAL_FAILURES(device(idx)->CheckResumeReceivedAndReply(state, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Then the fragments
  for (auto idx : fragment_device_indexes) {
    ASSERT_NO_FATAL_FAILURES(device(idx)->CheckResumeReceivedAndReply(state, ZX_OK));
  }
  coordinator_loop()->RunUntilIdle();

  // Then finally the composite device itself
  ASSERT_NO_FATAL_FAILURES(composite.CheckResumeReceivedAndReply(state, ZX_OK));
  coordinator_loop()->RunUntilIdle();
}

// Make sure we receive devfs notifications when composite devices appear
TEST_F(CompositeTestCase, DevfsNotifications) {
  zx::channel watcher;
  {
    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &watcher, &remote));
    ASSERT_OK(devfs_watch(coordinator().root_device()->self, std::move(remote),
                          fio::wire::kWatchMaskAdded));
  }

  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  DeviceState composite;
  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));

  uint8_t msg[fio::wire::kMaxFilename + 2];
  uint32_t msg_len = 0;
  ASSERT_OK(watcher.read(0, msg, nullptr, sizeof(msg), 0, &msg_len, nullptr));
  ASSERT_EQ(msg_len, 2 + strlen(kCompositeDevName));
  ASSERT_EQ(msg[0], fio::wire::kWatchEventAdded);
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
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDevName));
  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  DeviceState composite;
  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));

  Devnode* dn = coordinator().root_device()->self;
  fbl::RefPtr<Device> composite_dev;
  ASSERT_OK(devfs_walk(dn, "composite-dev", &composite_dev));

  char path_buf[PATH_MAX];
  ASSERT_OK(coordinator().GetTopologicalPath(composite_dev, path_buf, sizeof(path_buf)));
  ASSERT_STR_EQ(path_buf, "/dev/composite-dev");
}

class CompositeMetadataTestCase : public CompositeTestCase {
 public:
  enum class AddLocation {
    // Add the composite before any fragments
    BEFORE,
    // Add the composite after some fragments
    MIDDLE,
    // Add the composite after all fragments
    AFTER,
  };
  static constexpr uint32_t kMetadataKey = 999;
  static constexpr char kMetadataStr[] = "composite-metadata";

  void AddCompositeDevice(AddLocation add = AddLocation::BEFORE);

  void VerifyMetadata(void* data, size_t len) {
    ASSERT_EQ(strlen(kMetadataStr) + 1, len);
    ASSERT_BYTES_EQ(data, kMetadataStr, len);
  }

  fbl::RefPtr<Device> composite_device;

  // Hold reference to remote channels so that they do not close
  DeviceState composite;
};

void CompositeMetadataTestCase::AddCompositeDevice(AddLocation add) {
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

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
        platform_bus()->device, protocol_id, std::size(protocol_id), nullptr /* props */, 0,
        kCompositeDevName, ZX_OK, metadata, countof(metadata)));
  };

  if (add == AddLocation::BEFORE) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
    if (i == 0 && add == AddLocation::MIDDLE) {
      ASSERT_NO_FATAL_FAILURES(do_add());
    }
  }

  if (add == AddLocation::AFTER) {
    ASSERT_NO_FATAL_FAILURES(do_add());
  }

  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));
  composite_device = GetCompositeDeviceFromFragment(kCompositeDevName, device_indexes[0]);
  ASSERT_NOT_NULL(composite_device);
}

TEST_F(CompositeMetadataTestCase, AddAndGetMetadata) {
  char buf[32] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_OK(platform_bus()->device->coordinator->GetMetadata(composite_device, kMetadataKey, buf,
                                                             32, &len));
  VerifyMetadata(buf, len);
}

TEST_F(CompositeMetadataTestCase, FailGetMetadata) {
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_EQ(platform_bus()->device->coordinator->GetMetadata(composite_device, kMetadataKey + 1,
                                                             nullptr, 0, &len),
            ZX_ERR_NOT_FOUND);
}

TEST_F(CompositeMetadataTestCase, FailGetMetadataFromParent) {
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  fbl::RefPtr<Device> parent =
      composite_device->composite()->bound_fragments().front().bound_device();
  ASSERT_EQ(
      platform_bus()->device->coordinator->GetMetadata(parent, kMetadataKey, nullptr, 0, &len),
      ZX_ERR_NOT_FOUND);
}

TEST_F(CompositeMetadataTestCase, DefineAfterDevices) {
  char buf[32] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice(AddLocation::AFTER));
  ASSERT_OK(platform_bus()->device->coordinator->GetMetadata(composite_device, kMetadataKey, buf,
                                                             32, &len));
  VerifyMetadata(buf, len);
}

TEST_F(CompositeMetadataTestCase, DefineInBetweenDevices) {
  char buf[32] = "";
  size_t len = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice(AddLocation::MIDDLE));
  ASSERT_OK(platform_bus()->device->coordinator->GetMetadata(composite_device, kMetadataKey, buf,
                                                             32, &len));
  VerifyMetadata(buf, len);
}

TEST_F(CompositeMetadataTestCase, GetMetadataFromChild) {
  char buf[32] = "";
  size_t len = 0;
  size_t child_index = 0;
  ASSERT_NO_FATAL_FAILURES(AddCompositeDevice());
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(composite_device, "child", ZX_PROTOCOL_AUDIO, "", &child_index));
  fbl::RefPtr<Device> child = device(child_index)->device;
  ASSERT_OK(platform_bus()->device->coordinator->GetMetadata(child, kMetadataKey, buf, 32, &len));
  VerifyMetadata(buf, len);
}

// Make sure metadata exists after composite device is destroyed and re-created
// due to fragment removal and addition
TEST_F(CompositeMetadataTestCase, GetMetadataAfterCompositeReassemble) {
  char buf[32] = "";
  size_t len = 0;
  size_t device_indexes[3];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
      ZX_PROTOCOL_ETHERNET,
  };

  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const device_metadata_t metadata[] = {
      {
          .type = kMetadataKey,
          .data = const_cast<char*>(kMetadataStr),
          .length = strlen(kMetadataStr) + 1,
      },
  };

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(
      platform_bus()->device, protocol_id, std::size(protocol_id), nullptr /* props */, 0,
      kCompositeDevName, ZX_OK, metadata, countof(metadata)));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(
        AddDevice(platform_bus()->device, name, protocol_id[i], "", &device_indexes[i]));
  }

  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));
  composite_device = GetCompositeDeviceFromFragment(kCompositeDevName, device_indexes[0]);
  ASSERT_NOT_NULL(composite_device);

  // Get and verify metadata
  ASSERT_OK(platform_bus()->device->coordinator->GetMetadata(composite_device, kMetadataKey, buf,
                                                             32, &len));
  VerifyMetadata(buf, len);

  // Remove device 0 and its children (fragment and composite devices).
  ASSERT_NO_FATAL_FAILURES(coordinator().ScheduleRemove(device(device_indexes[0])->device));
  coordinator_loop()->RunUntilIdle();

  auto device_zero = device(device_indexes[0]);
  auto fragment = device(fragment_device_indexes[0]);

  // The device and composite should not have received an unbind request yet.
  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(composite.HasPendingMessages());

  // Check the fragment and composite are unbound.
  ASSERT_NO_FATAL_FAILURES(fragment->CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(fragment->HasPendingMessages());

  ASSERT_NO_FATAL_FAILURES(composite.CheckUnbindReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // Still waiting for the composite to be removed.
  ASSERT_FALSE(device_zero->HasPendingMessages());
  ASSERT_FALSE(fragment->HasPendingMessages());

  // Finish removing the composite.
  ASSERT_NO_FATAL_FAILURES(composite.CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_FALSE(device_zero->HasPendingMessages());

  // Finish removing the fragment.
  ASSERT_NO_FATAL_FAILURES(fragment->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  ASSERT_NO_FATAL_FAILURES(device_zero->CheckRemoveReceivedAndReply());
  coordinator_loop()->RunUntilIdle();

  // Add the device back and verify the composite gets created again
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "device-0", protocol_id[0], "", &device_indexes[0]));
  {
    auto device_state = device(device_indexes[0]);
    // Wait for the fragments to get bound
    fbl::String driver = coordinator().fragment_driver()->libname;
    ASSERT_NO_FATAL_FAILURES(device_state->CheckBindDriverReceivedAndReply(driver.data()));
    coordinator_loop()->RunUntilIdle();

    // Synthesize the AddDevice request the fragment driver would send
    ASSERT_NO_FATAL_FAILURES(AddDevice(device_state->device, "fragment-device-0", 0, driver,
                                       &fragment_device_indexes[0]));
  }
  ASSERT_NO_FATAL_FAILURES(CheckCreateCompositeDeviceReceived(
      driver_host_server(), kCompositeDevName, std::size(device_indexes), &composite));

  composite_device = GetCompositeDeviceFromFragment(kCompositeDevName, device_indexes[0]);
  ASSERT_NOT_NULL(composite_device);

  // Get and verify metadata again
  ASSERT_OK(platform_bus()->device->coordinator->GetMetadata(composite_device, kMetadataKey, buf,
                                                             32, &len));
  VerifyMetadata(buf, len);
}

// Tests that a composite is not created until the fragment devices finish initializing.
TEST_F(CompositeTestCase, FragmentDeviceInit) {
  size_t device_indexes[2];
  uint32_t protocol_id[] = {
      ZX_PROTOCOL_GPIO,
      ZX_PROTOCOL_I2C,
  };
  static_assert(std::size(protocol_id) == std::size(device_indexes));

  const char* kCompositeDevName = "composite-dev";
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, protocol_id,
                                                        std::size(protocol_id), nullptr /* props */,
                                                        0, kCompositeDevName));

  // Add the devices to construct the composite out of.
  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    char name[32];
    snprintf(name, sizeof(name), "device-%zu", i);
    ASSERT_NO_FATAL_FAILURES(AddDevice(platform_bus()->device, name, protocol_id[i], "",
                                       true /* has_init */, false /* reply_to_init */,
                                       true /* always_init */, zx::vmo() /* inspect */,
                                       &device_indexes[i]));
    auto index = device_indexes[i];
    ASSERT_FALSE(device(index)->device->is_visible());
    ASSERT_NO_FATAL_FAILURES(device(index)->CheckInitReceived());
    ASSERT_EQ(Device::State::kInitializing, device(index)->device->state());
    coordinator_loop()->RunUntilIdle();
  }

  for (size_t i = 0; i < std::size(device_indexes); ++i) {
    auto index = device_indexes[i];
    // Check that the fragment isn't being bound yet.
    ASSERT_FALSE(device(index)->HasPendingMessages());

    ASSERT_NO_FATAL_FAILURES(device(index)->SendInitReply());
    coordinator_loop()->RunUntilIdle();

    ASSERT_TRUE(device(index)->device->is_visible());
    ASSERT_EQ(Device::State::kActive, device(index)->device->state());
  }

  DeviceState composite;
  size_t fragment_device_indexes[std::size(device_indexes)];
  ASSERT_NO_FATAL_FAILURES(CheckCompositeCreation(kCompositeDevName, device_indexes,
                                                  std::size(device_indexes),
                                                  fragment_device_indexes, &composite));
  coordinator_loop()->RunUntilIdle();

  {
    fbl::RefPtr<Device> comp_device =
        GetCompositeDeviceFromFragment(kCompositeDevName, device_indexes[1]);
    ASSERT_NOT_NULL(comp_device);
    ASSERT_EQ(Device::State::kActive, comp_device->state());
  }
}

TEST_F(CompositeTestCase, DeviceIteratorCompositeChild) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent-device", 1 /* protocol id */, "", &parent_index));

  uint32_t protocol_id = 1;
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, &protocol_id, 1,
                                                        nullptr, 0, "composite"));

  DeviceState composite;
  size_t fragment_device_indexes;
  ASSERT_NO_FATAL_FAILURES(
      CheckCompositeCreation("composite", &parent_index, 1, &fragment_device_indexes, &composite));

  for (auto& d : device(parent_index)->device->children()) {
    ASSERT_EQ(d.name(), "composite-comp-device-0");
  }
}

TEST_F(CompositeTestCase, DeviceIteratorCompositeSibling) {
  size_t parent_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(platform_bus()->device, "parent-device", 1 /* protocol id */, "", &parent_index));

  uint32_t protocol_id = 1;
  ASSERT_NO_FATAL_FAILURES(BindCompositeDefineComposite(platform_bus()->device, &protocol_id, 1,
                                                        nullptr, 0, "composite"));
  DeviceState composite;
  size_t fragment_device_indexes;
  ASSERT_NO_FATAL_FAILURES(
      CheckCompositeCreation("composite", &parent_index, 1, &fragment_device_indexes, &composite));

  size_t child_index;
  ASSERT_NO_FATAL_FAILURES(
      AddDevice(device(parent_index)->device, "sibling-device", 0, "", &child_index));

  ASSERT_TRUE(device(child_index)->device->children().is_empty());
}
