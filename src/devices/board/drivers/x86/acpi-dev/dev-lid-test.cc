// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev-lid.h"

#include <inttypes.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-hidbus-ifc/mock-hidbus-ifc.h>
#include <zircon/syscalls/port.h>

#include <memory>

#include <acpica/acpi.h>
#include <zxtest/zxtest.h>

namespace {

// Container for mocked ACPI state. Used in conjunction with the CreateFake*Fn
// methods below to mock out ACPI functionality.
struct AcpiState {
  UINT64 lid_state = 0;
  ACPI_NOTIFY_HANDLER notify_handler = nullptr;
};

acpi_lid::AcpiObjectEvalFunc CreateFakeAcpiEvalFn(AcpiState* state) {
  return [state](ACPI_HANDLE handle, ACPI_STRING pathname, ACPI_OBJECT_LIST* external_params,
                 ACPI_BUFFER* return_buffer, ACPI_OBJECT_TYPE return_type) {
    EXPECT_EQ(return_type, ACPI_TYPE_INTEGER, "");
    EXPECT_STREQ(pathname, "_LID");
    static_cast<ACPI_OBJECT*>(return_buffer->Pointer)->Integer.Value = state->lid_state;
    return AE_OK;
  };
}

acpi_lid::AcpiInstallNotifyHandlerFunc CreateFakeAcpiInstallNotifyHandlerFn(AcpiState* state) {
  return [state](ACPI_HANDLE handle, UINT32 handler_type, ACPI_NOTIFY_HANDLER handler, void* ctx) {
    EXPECT_EQ(state->notify_handler, nullptr, "Handler already installed");
    EXPECT_EQ(handler_type, ACPI_DEVICE_NOTIFY);
    state->notify_handler = handler;
    return AE_OK;
  };
}

acpi_lid::AcpiRemoveNotifyHandlerFunc CreateFakeAcpiRemoveNotifyHandlerFn(AcpiState* state) {
  return [state](ACPI_HANDLE handle, UINT32 handler_type, ACPI_NOTIFY_HANDLER handler) {
    EXPECT_EQ(handler_type, ACPI_DEVICE_NOTIFY);
    EXPECT_EQ(handler, state->notify_handler);
    state->notify_handler = nullptr;
    return AE_OK;
  };
}

}  // namespace

namespace acpi_lid {

TEST(LidTest, Init) {
  {
    AcpiState state;
    state.lid_state = 1;
    std::unique_ptr<AcpiLidDevice> device;
    EXPECT_OK(AcpiLidDevice::Create(
        fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
        CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));
    EXPECT_EQ(device->State(), LidState::OPEN);
  }
  {
    AcpiState state;
    state.lid_state = 0;
    std::unique_ptr<AcpiLidDevice> device;
    EXPECT_OK(AcpiLidDevice::Create(
        fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
        CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));
    EXPECT_EQ(device->State(), LidState::CLOSED);
  }
}

TEST(LidTest, StartStopHid) {
  AcpiState state;
  state.lid_state = 1;
  std::unique_ptr<AcpiLidDevice> device;
  EXPECT_OK(AcpiLidDevice::Create(
      fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
      CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));
  ASSERT_EQ(device->State(), LidState::OPEN);

  mock_hidbus_ifc::MockHidbusIfc<uint8_t> mock_ifc;

  EXPECT_OK(device->HidbusStart(mock_ifc.proto()));
  EXPECT_NE(state.notify_handler, nullptr);

  device->HidbusStop();
  EXPECT_EQ(state.notify_handler, nullptr);
}

TEST(LidTest, HidGetDescriptor) {
  AcpiState state;
  state.lid_state = 0;
  std::unique_ptr<AcpiLidDevice> device;
  EXPECT_OK(AcpiLidDevice::Create(
      fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
      CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));

  uint8_t data[HID_MAX_DESC_LEN];
  size_t len;
  ASSERT_OK(device->HidbusGetDescriptor(HID_DESCRIPTION_TYPE_REPORT, data, sizeof(data), &len));
  ASSERT_EQ(len, AcpiLidDevice::kHidDescriptorLen);
  EXPECT_BYTES_EQ(data, AcpiLidDevice::kHidDescriptor, AcpiLidDevice::kHidDescriptorLen);
}

TEST(LidTest, HidGetReport) {
  {
    AcpiState state;
    state.lid_state = 0;
    std::unique_ptr<AcpiLidDevice> device;
    EXPECT_OK(AcpiLidDevice::Create(
        fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
        CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));

    uint8_t report;
    size_t copied_len;
    device->HidbusGetReport(HID_REPORT_TYPE_INPUT, 0, &report, sizeof(report), &copied_len);
    ASSERT_EQ(copied_len, 1U);
    EXPECT_EQ(report, 0U);
  }
  {
    AcpiState state;
    state.lid_state = 1;
    std::unique_ptr<AcpiLidDevice> device;
    EXPECT_OK(AcpiLidDevice::Create(
        fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
        CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));

    uint8_t report;
    size_t copied_len;
    device->HidbusGetReport(HID_REPORT_TYPE_INPUT, 0, &report, sizeof(report), &copied_len);
    ASSERT_EQ(copied_len, 1U);
    EXPECT_EQ(report, 1U);
  }
}

TEST(LidTest, UpdateState) {
  AcpiState state;
  state.lid_state = 1;
  std::unique_ptr<AcpiLidDevice> device;
  EXPECT_OK(AcpiLidDevice::Create(
      fake_ddk::kFakeParent, nullptr, &device, CreateFakeAcpiEvalFn(&state),
      CreateFakeAcpiInstallNotifyHandlerFn(&state), CreateFakeAcpiRemoveNotifyHandlerFn(&state)));
  ASSERT_EQ(device->State(), LidState::OPEN);
  mock_hidbus_ifc::MockHidbusIfc<uint8_t> mock_ifc;
  ASSERT_OK(device->HidbusStart(mock_ifc.proto()));
  ASSERT_NE(state.notify_handler, nullptr);

  // Simulate an ACPI state change notification
  state.lid_state = 0;
  state.notify_handler(nullptr, 0x80, device.get());
  ASSERT_EQ(device->State(), LidState::CLOSED);
  ASSERT_OK(mock_ifc.WaitForReports(1));
  uint8_t report = *mock_ifc.reports().begin();
  ASSERT_EQ(report, 0U);

  mock_ifc.Reset();

  // Simulate a spurious ACPI state change notification (without an actual
  // state change). No HID reports are expected.
  state.notify_handler(nullptr, 0x80, device.get());
  ASSERT_EQ(device->State(), LidState::CLOSED);
  ASSERT_EQ(mock_ifc.PendingReports(), 0U);

  // Simulate an ACPI state change notification
  state.lid_state = 1;
  state.notify_handler(nullptr, 0x80, device.get());
  ASSERT_EQ(device->State(), LidState::OPEN);
  ASSERT_OK(mock_ifc.WaitForReports(1));
  report = *mock_ifc.reports().begin();
  ASSERT_EQ(report, 1U);

  device->HidbusStop();
}

}  // namespace acpi_lid
