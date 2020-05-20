// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-report.h"

#include <threads.h>
#include <zircon/status.h>

#include <array>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/usages.h>

#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report_dev {

void InputReport::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

zx_status_t InputReport::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  auto inst = std::make_unique<InputReportInstance>(zxdev(), next_instance_id_++);
  zx_status_t status = inst->Bind(this);
  if (status != ZX_OK) {
    return status;
  }

  {
    fbl::AutoLock lock(&instance_lock_);
    instance_list_.push_front(inst.get());
  }

  // If we have a consumer control device, get a report and send it to the client,
  // since the client needs the device's state.
  for (auto& device : devices_) {
    if (device->GetDeviceType() == hid_input_report::DeviceType::kConsumerControl) {
      std::array<uint8_t, HID_MAX_REPORT_LEN> report_data;
      size_t report_size = 0;
      zx_status_t status = hiddev_.GetReport(HID_REPORT_TYPE_INPUT, device->InputReportId(),
                                             report_data.data(), report_data.size(), &report_size);
      if (status != ZX_OK) {
        continue;
      }
      inst->ReceiveReport(report_data.data(), report_size, zx_clock_get_monotonic(), device.get());
    }
  }

  *dev_out = inst->zxdev();

  // devmgr is now in charge of the memory for inst.
  __UNUSED auto ptr = inst.release();
  return ZX_OK;
}

void InputReport::HidReportListenerReceiveReport(const uint8_t* report, size_t report_size,
                                                 zx_time_t report_time) {
  for (auto& device : devices_) {
    // Find the matching device.
    if (device->InputReportId() != 0 && device->InputReportId() != report[0]) {
      continue;
    }

    // Send to each instance.
    {
      fbl::AutoLock lock(&instance_lock_);
      for (auto& instance : instance_list_) {
        instance.ReceiveReport(report, report_size, report_time, device.get());
      }
    }
  }
}

void InputReport::RemoveInstanceFromList(InputReportInstance* instance) {
  fbl::AutoLock lock(&instance_lock_);

  for (auto& iter : instance_list_) {
    if (iter.zxdev() == instance->zxdev()) {
      instance_list_.erase(iter);
      break;
    }
  }
}

bool InputReport::ParseHidInputReportDescriptor(const hid::ReportDescriptor* report_desc) {
  std::unique_ptr<hid_input_report::Device> device;
  hid_input_report::ParseResult result = hid_input_report::CreateDevice(report_desc, &device);
  if (result != hid_input_report::ParseResult::kOk) {
    return false;
  }
  devices_.push_back(std::move(device));
  return true;
}

void InputReport::CreateDescriptor(fidl::Allocator* allocator,
                                   fuchsia_input_report::DeviceDescriptor::Builder* descriptor) {
  hid_device_info_t info;
  hiddev_.GetHidDeviceInfo(&info);

  fuchsia_input_report::DeviceInfo fidl_info;
  fidl_info.vendor_id = info.vendor_id;
  fidl_info.product_id = info.product_id;
  fidl_info.version = info.version;
  descriptor->set_device_info(
      allocator->make<fuchsia_input_report::DeviceInfo>(std::move(fidl_info)));

  for (auto& device : devices_) {
    device->CreateDescriptor(allocator, descriptor);
  }
}

zx_status_t InputReport::SendOutputReport(fuchsia_input_report::OutputReport report) {
  uint8_t hid_report[HID_MAX_DESC_LEN];
  size_t size;
  hid_input_report::ParseResult result = hid_input_report::ParseResult::kNotImplemented;
  for (auto& device : devices_) {
    result = device->SetOutputReport(&report, hid_report, sizeof(hid_report), &size);
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
    return ZX_ERR_INTERNAL;
  }
  return hiddev_.SetReport(HID_REPORT_TYPE_OUTPUT, hid_report[0], hid_report, size);
}

zx_status_t InputReport::Bind() {
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
    return false;
  }

  auto count = dev_desc->rep_count;
  if (count == 0) {
    zxlogf(ERROR, "No report descriptors found ");
    return false;
  }

  // Parse each input report.
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportDescriptor* desc = &dev_desc->report[rep];
    if (desc->input_count != 0) {
      if (!ParseHidInputReportDescriptor(desc)) {
        continue;
      }
    }
  }

  // If we never parsed a single device correctly then fail.
  if (devices_.size() == 0) {
    zxlogf(ERROR, "Can't process HID report descriptor for, all parsing attempts failed.");
    return ZX_ERR_INTERNAL;
  }

  // Register to listen to HID reports.
  hiddev_.RegisterListener(this, &hid_report_listener_protocol_ops_);

  return DdkAdd("InputReport");
}

zx_status_t input_report_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;

  ddk::HidDeviceProtocolClient hiddev(parent);
  if (!hiddev.is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  auto dev = fbl::make_unique_checked<InputReport>(&ac, parent, hiddev);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t input_report_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = input_report_bind;
  return ops;
}();

}  // namespace hid_input_report_dev

// clang-format off
ZIRCON_DRIVER_BEGIN(InputReport, hid_input_report_dev::input_report_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_HID_DEVICE),
ZIRCON_DRIVER_END(inputReport)
    // clang-format on
