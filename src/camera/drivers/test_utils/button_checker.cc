// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "button_checker.h"

#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include <fbl/unique_fd.h>

#include "src/lib/syslog/cpp/logger.h"

constexpr auto kDevicePath = "/dev/class/input";
constexpr auto kTag = "button_checker";

std::unique_ptr<ButtonChecker> ButtonChecker::Create() {
  auto checker = std::make_unique<ButtonChecker>();

  std::error_code ec{};
  auto it = std::filesystem::directory_iterator(kDevicePath, ec);
  if (ec) {
    FX_LOGST(ERROR, kTag) << "Unable to open " << kDevicePath << ": " << ec.message();
    return nullptr;
  }
  for (const auto& entry : it) {
    auto device = BindDevice(entry.path());
    hid::ReportField mute_field{};
    if (device && !GetMuteFieldForDevice(device, &mute_field)) {
      checker->devices_.push_back(std::make_pair(std::move(device), std::move(mute_field)));
    }
  }

  if (checker->devices_.size() == 0) {
    FX_LOGST(WARNING, kTag) << "Zero devices were bound from " << kDevicePath;
    return nullptr;
  }

  return checker;
}

ButtonChecker::ButtonState ButtonChecker::GetMuteState() {
  auto state = ButtonState::UNKNOWN;
  for (auto& device : devices_) {
    // Get the report for the mute field.
    zx_status_t status_return = ZX_OK;
    std::vector<uint8_t> report;
    zx_status_t status = device.first->GetReport(fuchsia::hardware::input::ReportType::INPUT,
                                                 device.second.report_id, &status_return, &report);
    if (status != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status);
      return ButtonState::UNKNOWN;
    }
    if (status_return != ZX_OK) {
      FX_PLOGST(ERROR, kTag, status_return);
      return ButtonState::UNKNOWN;
    }

    // Extract the value from the report.
    double field_value = 0.0;
    if (!hid::ExtractAsUnit(report.data(), report.size(), device.second.attr, &field_value)) {
      FX_LOGST(ERROR, kTag) << "Failed to extract HID field value";
      return ButtonState::UNKNOWN;
    }
    ButtonState state_for_device = field_value > 0 ? ButtonState::DOWN : ButtonState::UP;

    // Make sure that devices don't have conflicting states.
    if (state != ButtonState::UNKNOWN && state != state_for_device) {
      FX_LOGST(ERROR, kTag) << "Conflicting states reported by different devices";
      return ButtonState::UNKNOWN;
    }
    state = state_for_device;
  }

  return state;
}

fuchsia::hardware::input::DeviceSyncPtr ButtonChecker::BindDevice(const std::string& path) {
  // Open the device.
  int result = open(path.c_str(), O_RDONLY);
  if (result < 0) {
    FX_LOGST(ERROR, kTag) << "Error accessing " << path;
    return nullptr;
  }
  fbl::unique_fd fd(result);

  // Get a channel to its services.
  zx::channel channel;
  zx_status_t status = fdio_get_service_handle(fd.get(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Error getting channel from device " << path;
    return nullptr;
  }

  // Bind to the channel.
  fuchsia::hardware::input::DeviceSyncPtr device;
  device.Bind(std::move(channel));

  return device;
}

bool ButtonChecker::GetMuteFieldForDevice(fuchsia::hardware::input::DeviceSyncPtr& device,
                                          hid::ReportField* mute_field_out) {
  FX_CHECK(mute_field_out);
  auto kMuteButtonUsage =
      hid::USAGE(hid::usage::Page::kTelephony, hid::usage::Telephony::kPhoneMute);

  // Get the report descriptor.
  std::vector<uint8_t> desc;
  zx_status_t status = device->GetReportDesc(&desc);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status);
    return true;
  }

  // Parse the descriptor.
  hid::DeviceDescriptor* device_descriptor_ptr = nullptr;
  auto parse_result = hid::ParseReportDescriptor(desc.data(), desc.size(), &device_descriptor_ptr);
  if (parse_result != hid::kParseOk) {
    FX_LOGST(ERROR, kTag) << "HID Parse Failure: " << parse_result;
    return true;
  }
  std::unique_ptr<hid::DeviceDescriptor, decltype(&hid::FreeDeviceDescriptor)> device_descriptor(
      device_descriptor_ptr, &hid::FreeDeviceDescriptor);

  // Loop over the descriptor reports and their fields.
  bool found_mute_field = false;
  for (size_t report_index = 0; report_index < device_descriptor->rep_count; ++report_index) {
    const auto& report_descriptor = device_descriptor->report[report_index];
    for (size_t field_index = 0; field_index < report_descriptor.input_count; ++field_index) {
      const auto& input_field = report_descriptor.input_fields[field_index];

      // If the report desc is for the mute button, save out the field metadata.
      if (input_field.attr.usage == kMuteButtonUsage) {
        if (found_mute_field && memcmp(mute_field_out, &input_field, sizeof(input_field)) != 0) {
          FX_LOGST(ERROR, kTag) << "Multiple mute fields found in same device";
          return true;
        }
        *mute_field_out = input_field;
        found_mute_field = true;
      }
    }
  }

  return !found_mute_field;
}

bool VerifyDeviceUnmuted(bool consider_unknown_as_unmuted) {
  auto state = ButtonChecker::ButtonState::UNKNOWN;
  auto checker = ButtonChecker::Create();
  if (checker) {
    state = checker->GetMuteState();
  }
  if (state == ButtonChecker::ButtonState::UP) {
    return true;
  }
  if (state == ButtonChecker::ButtonState::UNKNOWN) {
    std::cerr << "**************************************************\n"
                 "* WARNING: DEVICE MUTE STATE UNKNOWN. CAMERA MAY *\n"
                 "*          NOT OPERATE AND TESTS MAY BE SKIPPED! *\n"
                 "**************************************************\n";
    std::cerr.flush();
    return consider_unknown_as_unmuted;
  }
  FX_DCHECK(state == ButtonChecker::ButtonState::DOWN);
  std::cerr << "**********************************************\n"
               "* WARNING: DEVICE IS MUTED. CAMERA WILL NOT  *\n"
               "*          OPERATE AND TESTS MAY BE SKIPPED! *\n"
               "**********************************************\n";
  std::cerr.flush();
  return false;
}
