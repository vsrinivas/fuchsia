// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/fdio_hid_decoder.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <unistd.h>
#include <zircon/status.h>

#include <hid/acer12.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <trace/event.h>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"

namespace {

bool log_err(zx_status_t status, const std::string& what, const std::string& name) {
  FXL_LOG(ERROR) << "hid: could not get " << what << " from " << name
                 << " (status=" << zx_status_get_string(status) << ")";
  return false;
}

}  // namespace

namespace ui_input {

FdioHidDecoder::FdioHidDecoder(const std::string& name, fbl::unique_fd fd)
    : caller_(std::move(fd)), name_(name) {}

FdioHidDecoder::~FdioHidDecoder() = default;

bool FdioHidDecoder::Init() {
  zx_status_t status;
  zx_handle_t svc = caller_.borrow_channel();

  // Get the Boot Protocol if there is one.
  fuchsia_hardware_input_BootProtocol boot_protocol;
  status = fuchsia_hardware_input_DeviceGetBootProtocol(svc, &boot_protocol);
  if (status != ZX_OK) {
    return log_err(status, "boot protocol", name_);
  }

  if (boot_protocol == fuchsia_hardware_input_BootProtocol_KBD) {
    boot_mode_ = BootMode::KEYBOARD;
  } else if (boot_protocol == fuchsia_hardware_input_BootProtocol_MOUSE) {
    boot_mode_ = BootMode::MOUSE;
  } else {
    boot_mode_ = BootMode::NONE;
  }

  // Get the report descriptor.
  uint16_t report_desc_len;
  status = fuchsia_hardware_input_DeviceGetReportDescSize(svc, &report_desc_len);
  if (status != ZX_OK)
    return log_err(status, "report descriptor length", name_);

  report_descriptor_.resize(report_desc_len);
  size_t actual;
  status = fuchsia_hardware_input_DeviceGetReportDesc(svc, report_descriptor_.data(),
                                                      report_descriptor_.size(), &actual);
  if (status != ZX_OK)
    return log_err(status, "report descriptor", name_);
  report_descriptor_.resize(actual);

  // Use lower 32 bits of channel koid as trace ID.
  zx_info_handle_basic_t info;
  zx_object_get_info(svc, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  trace_id_ = info.koid & 0xffffffff;
  status = fuchsia_hardware_input_DeviceSetTraceId(svc, trace_id_);
  if (status != ZX_OK)
    return log_err(status, "failed to set trace ID", name_);

  return true;
}

zx::event FdioHidDecoder::GetEvent() {
  zx::event event;

  auto resp = ::llcpp::fuchsia::device::Controller::Call::GetEventHandle(
      zx::unowned_channel(caller_.borrow_channel()));
  zx_status_t status = resp.status();
  if (status == ZX_OK) {
    event = std::move(resp->event);
  }
  if (status != ZX_OK) {
    log_err(status, "event handle", name_);
    return {};
  }
  return event;
}

const std::vector<uint8_t>& FdioHidDecoder::ReadReportDescriptor(int* bytes_read) {
  *bytes_read = report_descriptor_.size();
  return report_descriptor_;
}

zx_status_t FdioHidDecoder::Read(uint8_t* data, size_t data_size, size_t* report_size,
                                 zx_time_t* timestamp) {
  zx_status_t call_status;
  zx_status_t status = fuchsia_hardware_input_DeviceReadReport(
      caller_.borrow_channel(), &call_status, data, data_size, report_size, timestamp);
  if (status != ZX_OK) {
    return status;
  }
  return call_status;
}

zx_status_t FdioHidDecoder::Send(ReportType type, uint8_t report_id,
                                 const std::vector<uint8_t>& report) {
  FXL_CHECK(type != ReportType::INPUT);

  fuchsia_hardware_input_ReportType fidl_report_type;
  switch (type) {
    case ReportType::INPUT:
      fidl_report_type = fuchsia_hardware_input_ReportType_INPUT;
      break;
    case ReportType::OUTPUT:
      fidl_report_type = fuchsia_hardware_input_ReportType_OUTPUT;
      break;
    case ReportType::FEATURE:
      fidl_report_type = fuchsia_hardware_input_ReportType_FEATURE;
      break;
  }

  zx_handle_t svc = caller_.borrow_channel();

  zx_status_t call_status;
  zx_status_t status = fuchsia_hardware_input_DeviceSetReport(
      svc, fidl_report_type, report_id, report.data(), report.size(), &call_status);

  if (status != ZX_OK) {
    return status;
  } else if (call_status != ZX_OK) {
    return call_status;
  }
  return ZX_OK;
}

zx_status_t FdioHidDecoder::GetReport(ReportType type, uint8_t report_id,
                                      std::vector<uint8_t>* report) {
  zx_status_t res, call_status;
  uint16_t size;

  fuchsia_hardware_input_ReportType real_type;
  switch (type) {
    case ReportType::INPUT:
      real_type = fuchsia_hardware_input_ReportType_INPUT;
      break;
    case ReportType::OUTPUT:
      real_type = fuchsia_hardware_input_ReportType_OUTPUT;
      break;
    case ReportType::FEATURE:
      real_type = fuchsia_hardware_input_ReportType_FEATURE;
      break;
  }

  res = fuchsia_hardware_input_DeviceGetReportSize(caller_.borrow_channel(), real_type, report_id,
                                                   &call_status, &size);
  if (res != ZX_OK || call_status != ZX_OK) {
    FXL_LOG(ERROR) << "hid: could not get report (id " << report_id << " type " << real_type
                   << ") size (status=" << zx_status_get_string(res) << ", "
                   << zx_status_get_string(call_status) << ")";
    return call_status;
  }

  report->resize(size);

  size_t actual;
  res =
      fuchsia_hardware_input_DeviceGetReport(caller_.borrow_channel(), real_type, report_id,
                                             &call_status, report->data(), report->size(), &actual);
  if (res != ZX_OK || call_status != ZX_OK) {
    FXL_LOG(ERROR) << "hid: could not get report: " << zx_status_get_string(res) << " "
                   << zx_status_get_string(call_status);
    return call_status;
  }
  report->resize(actual);

  return ZX_OK;
}

}  // namespace ui_input
