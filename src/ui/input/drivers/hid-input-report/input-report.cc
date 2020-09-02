// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "input-report.h"

#include <lib/fidl/epitaph.h>
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

zx_status_t InputReport::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_input_report::InputDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
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

void InputReport::SendInitialConsumerControlReport(InputReportsReader* reader) {
  for (auto& device : devices_) {
    if (device->GetDeviceType() == hid_input_report::DeviceType::kConsumerControl) {
      std::array<uint8_t, HID_MAX_REPORT_LEN> report_data;
      size_t report_size = 0;
      zx_status_t status = hiddev_.GetReport(HID_REPORT_TYPE_INPUT, device->InputReportId(),
                                             report_data.data(), report_data.size(), &report_size);
      if (status != ZX_OK) {
        continue;
      }
      reader->ReceiveReport(report_data.data(), report_size, zx_clock_get_monotonic(),
                            device.get());
    }
  }
}

void InputReport::GetInputReportsReader(zx::channel req,
                                        GetInputReportsReaderCompleter::Sync completer) {
  fbl::AutoLock lock(&readers_lock_);

  auto reader =
      InputReportsReader::Create(this, next_reader_id_++, loop_->dispatcher(), std::move(req));
  if (!reader) {
    return;
  }

  SendInitialConsumerControlReport(reader.get());
  readers_list_.push_back(std::move(reader));

  // Signal to a test framework (if it exists) that we are connected to a reader.
  sync_completion_signal(&next_reader_wait_);
}

void InputReport::GetDescriptor(GetDescriptorCompleter::Sync completer) {
  fidl::BufferThenHeapAllocator<kFidlDescriptorBufferSize> descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());

  hid_device_info_t info;
  hiddev_.GetHidDeviceInfo(&info);

  fuchsia_input_report::DeviceInfo fidl_info;
  fidl_info.vendor_id = info.vendor_id;
  fidl_info.product_id = info.product_id;
  fidl_info.version = info.version;
  descriptor_builder.set_device_info(
      descriptor_allocator.make<fuchsia_input_report::DeviceInfo>(std::move(fidl_info)));

  for (auto& device : devices_) {
    device->CreateDescriptor(&descriptor_allocator, &descriptor_builder);
  }

  fidl::Result result = completer.Reply(descriptor_builder.build());
  if (result.status() != ZX_OK) {
    zxlogf(ERROR, "GetDescriptor: Failed to send descriptor (%s): %s\n", result.status_string(),
           result.error());
  }
}

void InputReport::SendOutputReport(fuchsia_input_report::OutputReport report,
                                   SendOutputReportCompleter::Sync completer) {
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
    return ZX_ERR_INTERNAL;
  }

  auto count = dev_desc->rep_count;
  if (count == 0) {
    zxlogf(ERROR, "No report descriptors found ");
    return ZX_ERR_INTERNAL;
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

  // Start the async loop for the Readers.
  {
    fbl::AutoLock lock(&readers_lock_);
    loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
    status = loop_->StartThread("hid-input-report-reader-loop");
    if (status != ZX_OK) {
      return status;
    }
  }

  return DdkAdd("InputReport");
}

zx_status_t InputReport::WaitForNextReader(zx::duration timeout) {
  zx_status_t status = sync_completion_wait(&next_reader_wait_, timeout.get());
  if (status == ZX_OK) {
    sync_completion_reset(&next_reader_wait_);
  }
  return status;
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
