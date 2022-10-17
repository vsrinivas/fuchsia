// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-report.h"

#include <lib/ddk/debug.h>
#include <lib/fidl/epitaph.h>
#include <lib/fit/defer.h>
#include <lib/trace/internal/event_common.h>
#include <lib/zx/clock.h>
#include <threads.h>
#include <zircon/status.h>

#include <array>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/usages.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report_dev {

zx::result<hid_input_report::DeviceType> InputReport::InputReportDeviceTypeToHid(
    const fuchsia_input_report::wire::DeviceType type) {
  switch (type) {
    case fuchsia_input_report::wire::DeviceType::kMouse:
      return zx::ok(hid_input_report::DeviceType::kMouse);
    case fuchsia_input_report::wire::DeviceType::kSensor:
      return zx::ok(hid_input_report::DeviceType::kSensor);
    case fuchsia_input_report::wire::DeviceType::kTouch:
      return zx::ok(hid_input_report::DeviceType::kTouch);
    case fuchsia_input_report::wire::DeviceType::kKeyboard:
      return zx::ok(hid_input_report::DeviceType::kKeyboard);
    case fuchsia_input_report::wire::DeviceType::kConsumerControl:
      return zx::ok(hid_input_report::DeviceType::kConsumerControl);
    default:
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

zx_status_t InputReport::Stop() {
  hiddev_.UnregisterListener();
  return ZX_OK;
}

void InputReport::RemoveReaderFromList(InputReportsReader* reader) {
  fbl::AutoLock lock(&readers_lock_);
  for (auto iter = readers_list_.begin(); iter != readers_list_.end(); ++iter) {
    if (iter->get() == reader) {
      readers_list_.erase(iter);
      break;
    }
  }
}

void InputReport::HidReportListenerReceiveReport(const uint8_t* report, size_t report_size,
                                                 zx_time_t report_time) {
  fbl::AutoLock lock(&readers_lock_);
  for (auto& device : devices_) {
    // Find the matching device.
    if (device->InputReportId() != 0 && device->InputReportId() != report[0]) {
      continue;
    }

    for (auto& reader : readers_list_) {
      reader->ReceiveReport(report, report_size, report_time, device.get());
    }
  }

  const zx::duration latency = zx::clock::get_monotonic() - zx::time(report_time);

  total_latency_ += latency;
  report_count_++;
  average_latency_usecs_.Set(total_latency_.to_usecs() / report_count_);

  if (latency > max_latency_) {
    max_latency_ = latency;
    max_latency_usecs_.Set(max_latency_.to_usecs());
  }

  latency_histogram_usecs_.Insert(latency.to_usecs());
}

bool InputReport::ParseHidInputReportDescriptor(const hid::ReportDescriptor* report_desc) {
  std::unique_ptr<hid_input_report::Device> device;
  hid_input_report::ParseResult result = hid_input_report::CreateDevice(report_desc, &device);
  if (result != hid_input_report::ParseResult::kOk) {
    return false;
  }
  if (device->GetDeviceType() == hid_input_report::DeviceType::kSensor) {
    sensor_count_++;
  }
  devices_.push_back(std::move(device));
  return true;
}

void InputReport::SendInitialConsumerControlReport(InputReportsReader* reader) {
  for (auto& device : devices_) {
    if (device->GetDeviceType() == hid_input_report::DeviceType::kConsumerControl) {
      if (!device->InputReportId().has_value()) {
        continue;
      }

      std::array<uint8_t, HID_MAX_REPORT_LEN> report_data;
      size_t report_size = 0;
      zx_status_t status = hiddev_.GetReport(HID_REPORT_TYPE_INPUT, *device->InputReportId(),
                                             report_data.data(), report_data.size(), &report_size);
      if (status != ZX_OK) {
        continue;
      }
      reader->ReceiveReport(report_data.data(), report_size, zx_clock_get_monotonic(),
                            device.get());
    }
  }
}

std::string InputReport::GetDeviceTypesString() const {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
  const char* kDeviceTypeNames[] = {
      [static_cast<uint32_t>(hid_input_report::DeviceType::kMouse)] = "mouse",
      [static_cast<uint32_t>(hid_input_report::DeviceType::kSensor)] = "sensor",
      [static_cast<uint32_t>(hid_input_report::DeviceType::kTouch)] = "touch",
      [static_cast<uint32_t>(hid_input_report::DeviceType::kKeyboard)] = "keyboard",
      [static_cast<uint32_t>(hid_input_report::DeviceType::kConsumerControl)] = "consumer-control",
  };
#pragma GCC diagnostic pop

  std::string device_types;
  for (size_t i = 0; i < devices_.size(); i++) {
    if (i > 0) {
      device_types += ',';
    }

    const auto type = static_cast<uint32_t>(devices_[i]->GetDeviceType());
    if (type >= sizeof(kDeviceTypeNames) || !kDeviceTypeNames[type]) {
      device_types += "unknown";
    } else {
      device_types += kDeviceTypeNames[type];
    }
  }

  return device_types;
}

void InputReport::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                        GetInputReportsReaderCompleter::Sync& completer) {
  fbl::AutoLock lock(&readers_lock_);

  auto reader = InputReportsReader::Create(this, next_reader_id_++, loop_->dispatcher(),
                                           std::move(request->reader));
  if (!reader) {
    return;
  }

  SendInitialConsumerControlReport(reader.get());
  readers_list_.push_back(std::move(reader));

  // Signal to a test framework (if it exists) that we are connected to a reader.
  sync_completion_signal(&next_reader_wait_);
}

void InputReport::GetDescriptor(GetDescriptorCompleter::Sync& completer) {
  fidl::Arena<kFidlDescriptorBufferSize> descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);

  hid_device_info_t info;
  hiddev_.GetHidDeviceInfo(&info);

  fuchsia_input_report::wire::DeviceInfo fidl_info;
  fidl_info.vendor_id = info.vendor_id;
  fidl_info.product_id = info.product_id;
  fidl_info.version = info.version;
  descriptor.set_device_info(descriptor_allocator, std::move(fidl_info));

  if (sensor_count_) {
    fidl::VectorView<fuchsia_input_report::wire::SensorInputDescriptor> input(descriptor_allocator,
                                                                              sensor_count_);
    fuchsia_input_report::wire::SensorDescriptor sensor(descriptor_allocator);
    sensor.set_input(descriptor_allocator, std::move(input));
    descriptor.set_sensor(descriptor_allocator, std::move(sensor));
  }

  for (auto& device : devices_) {
    device->CreateDescriptor(descriptor_allocator, descriptor);
  }

  completer.Reply(std::move(descriptor));
  fidl::Status result = completer.result_of_reply();
  if (result.status() != ZX_OK) {
    zxlogf(ERROR, "GetDescriptor: Failed to send descriptor: %s\n",
           result.FormatDescription().c_str());
  }
}

void InputReport::SendOutputReport(SendOutputReportRequestView request,
                                   SendOutputReportCompleter::Sync& completer) {
  uint8_t hid_report[HID_MAX_DESC_LEN];
  size_t size;
  hid_input_report::ParseResult result = hid_input_report::ParseResult::kNotImplemented;
  for (auto& device : devices_) {
    result = device->SetOutputReport(&request->report, hid_report, sizeof(hid_report), &size);
    if (result == hid_input_report::ParseResult::kOk) {
      break;
    }
    // Returning an error other than kParseNotImplemented means the device was supposed
    // to set the Output report but hit an error. When this happens we return the error.
    if (result != hid_input_report::ParseResult::kNotImplemented) {
      break;
    }
  }
  if (result != hid_input_report::ParseResult::kOk) {
    completer.ReplyError(ZX_ERR_INTERNAL);
    return;
  }

  zx_status_t status = hiddev_.SetReport(HID_REPORT_TYPE_OUTPUT, hid_report[0], hid_report, size);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

void InputReport::GetFeatureReport(GetFeatureReportCompleter::Sync& completer) {
  fidl::Arena<kFidlDescriptorBufferSize> allocator;
  fuchsia_input_report::wire::FeatureReport report(allocator);

  for (auto& device : devices_) {
    if (!device->FeatureReportId().has_value()) {
      continue;
    }

    std::array<uint8_t, HID_MAX_REPORT_LEN> report_data;
    size_t report_size = 0;
    auto status = hiddev_.GetReport(HID_REPORT_TYPE_FEATURE, *device->FeatureReportId(),
                                    report_data.data(), report_data.size(), &report_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "GetReport failed %d", status);
      completer.ReplyError(status);
      return;
    }

    auto result = device->ParseFeatureReport(report_data.data(), report_size, allocator, report);
    if (result != hid_input_report::ParseResult::kOk &&
        result != hid_input_report::ParseResult::kNotImplemented) {
      zxlogf(ERROR, "ParseFeatureReport failed with %u", result);
      completer.ReplyError(ZX_ERR_INTERNAL);
      return;
    }
  }

  completer.ReplySuccess(std::move(report));
  fidl::Status result = completer.result_of_reply();
  if (result.status() != ZX_OK) {
    zxlogf(ERROR, "Failed to get feature report: %s\n", result.FormatDescription().c_str());
  }
}

void InputReport::SetFeatureReport(SetFeatureReportRequestView request,
                                   SetFeatureReportCompleter::Sync& completer) {
  bool found = false;
  for (auto& device : devices_) {
    if (!device->FeatureReportId().has_value()) {
      continue;
    }

    uint8_t hid_report[HID_MAX_DESC_LEN];
    size_t size;
    auto result = device->SetFeatureReport(&request->report, hid_report, sizeof(hid_report), &size);
    if (result == hid_input_report::ParseResult::kNotImplemented ||
        result == hid_input_report::ParseResult::kItemNotFound) {
      continue;
    }
    if (result != hid_input_report::ParseResult::kOk) {
      zxlogf(ERROR, "SetFeatureReport failed with %u", result);
      completer.ReplyError(ZX_ERR_INTERNAL);
      return;
    }
    zx_status_t status =
        hiddev_.SetReport(HID_REPORT_TYPE_FEATURE, *device->FeatureReportId(), hid_report, size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "SetReport failed with %u", status);
      completer.ReplyError(status);
      return;
    }
    found = true;
  }

  if (!found) {
    completer.ReplyError(ZX_ERR_INTERNAL);
    return;
  }
  completer.ReplySuccess();
  fidl::Status result = completer.result_of_reply();
  if (result.status() != ZX_OK) {
    zxlogf(ERROR, "Failed to set feature report: %s\n", result.FormatDescription().c_str());
  }
}

void InputReport::GetInputReport(GetInputReportRequestView request,
                                 GetInputReportCompleter::Sync& completer) {
  const auto device_type = InputReportDeviceTypeToHid(request->device_type);
  if (device_type.is_error()) {
    completer.ReplyError(device_type.error_value());
    return;
  }

  fidl::Arena allocator;
  fuchsia_input_report::wire::InputReport report(allocator);

  for (auto& device : devices_) {
    if (!device->InputReportId().has_value()) {
      continue;
    }
    if (device->GetDeviceType() != device_type.value()) {
      continue;
    }
    if (report.has_event_time()) {
      // GetInputReport is not supported with multiple devices of the same type, as there is no
      // way to distinguish between them.
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }

    std::array<uint8_t, HID_MAX_REPORT_LEN> report_data;
    size_t report_size = 0;
    zx_status_t status = hiddev_.GetReport(HID_REPORT_TYPE_INPUT, *device->InputReportId(),
                                           report_data.data(), report_data.size(), &report_size);
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }

    if (device->ParseInputReport(report_data.data(), report_size, allocator, report) !=
        hid_input_report::ParseResult::kOk) {
      zxlogf(ERROR, "GetInputReport: Device failed to parse report correctly");
      completer.ReplyError(ZX_ERR_INTERNAL);
      return;
    }

    report.set_report_id(*device->InputReportId());
    report.set_event_time(allocator, zx_clock_get_monotonic());
    report.set_trace_id(allocator, TRACE_NONCE());
  }

  if (report.has_event_time()) {
    completer.ReplySuccess(report);
  } else {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
}

zx_status_t InputReport::Start() {
  uint8_t report_desc[HID_MAX_DESC_LEN];
  size_t report_desc_size;
  zx_status_t status = hiddev_.GetDescriptor(report_desc, HID_MAX_DESC_LEN, &report_desc_size);
  if (status != ZX_OK) {
    return status;
  }

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(report_desc, report_desc_size, &dev_desc);
  if (parse_res != hid::ParseResult::kParseOk) {
    zxlogf(ERROR, "hid-parser: parsing report descriptor failed with error %d", int(parse_res));
    return ZX_ERR_INTERNAL;
  }
  auto free_desc = fit::defer([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });

  auto count = dev_desc->rep_count;
  if (count == 0) {
    zxlogf(ERROR, "No report descriptors found ");
    return ZX_ERR_INTERNAL;
  }

  // Parse each input report.
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportDescriptor* desc = &dev_desc->report[rep];
    if (!ParseHidInputReportDescriptor(desc)) {
      continue;
    }
  }

  // If we never parsed a single device correctly then fail.
  if (devices_.size() == 0) {
    zxlogf(ERROR, "Can't process HID report descriptor for, all parsing attempts failed.");
    return ZX_ERR_INTERNAL;
  }

  // Start the async loop for the Readers.
  {
    fbl::AutoLock lock(&readers_lock_);
    loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
    status = loop_->StartThread("hid-input-report-reader-loop");
    if (status != ZX_OK) {
      return status;
    }
  }

  // Register to listen to HID reports.
  status = hiddev_.RegisterListener(this, &hid_report_listener_protocol_ops_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to register for HID reports: %s", zx_status_get_string(status));
    return status;
  }

  const std::string device_types = GetDeviceTypesString();

  root_ = inspector_.GetRoot().CreateChild("hid-input-report-" + device_types);
  latency_histogram_usecs_ = root_.CreateExponentialUintHistogram(
      "latency_histogram_usecs", kLatencyFloor.to_usecs(), kLatencyInitialStep.to_usecs(),
      kLatencyStepMultiplier, kLatencyBucketCount);
  average_latency_usecs_ = root_.CreateUint("average_latency_usecs", 0);
  max_latency_usecs_ = root_.CreateUint("max_latency_usecs", 0);
  device_types_ = root_.CreateString("device_types", device_types);

  return ZX_OK;
}

zx_status_t InputReport::WaitForNextReader(zx::duration timeout) {
  zx_status_t status = sync_completion_wait(&next_reader_wait_, timeout.get());
  if (status == ZX_OK) {
    sync_completion_reset(&next_reader_wait_);
  }
  return status;
}

}  // namespace hid_input_report_dev
