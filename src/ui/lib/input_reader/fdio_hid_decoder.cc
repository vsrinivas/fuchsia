// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/fdio_hid_decoder.h"

#include <lib/fdio/fdio.h>
#include <unistd.h>
#include <zircon/status.h>

#include <hid/acer12.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <trace/event.h>

#include "src/lib/files/unique_fd.h"
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

FdioHidDecoder::FdioHidDecoder(const std::string& name, fxl::UniqueFD fd)
    : name_(name), fd_(std::move(fd)) {}

FdioHidDecoder::~FdioHidDecoder() = default;

bool FdioHidDecoder::Init() {
  zx::channel chan;
  zx_status_t status = fdio_get_service_handle(fd_.release(), chan.reset_and_get_address());
  if (status != ZX_OK) {
    return false;
  }
  device_ = llcpp::fuchsia::hardware::input::Device::SyncClient(std::move(chan));

  // Get the Boot Protocol if there is one.
  {
    auto result = device_.GetBootProtocol();
    if (result.status() != ZX_OK) {
      return log_err(status, "boot protocol", name_);
    }
    if (result->protocol == llcpp::fuchsia::hardware::input::BootProtocol::KBD) {
      boot_mode_ = BootMode::KEYBOARD;
    } else if (result->protocol == llcpp::fuchsia::hardware::input::BootProtocol::MOUSE) {
      boot_mode_ = BootMode::MOUSE;
    } else {
      boot_mode_ = BootMode::NONE;
    }
  }

  // Get the report descriptor.
  {
    auto result = device_.GetReportDesc();
    if (result.status() != ZX_OK)
      return log_err(status, "report descriptor", name_);

    report_descriptor_.resize(result->desc.count());
    memcpy(report_descriptor_.data(), result->desc.data(), result->desc.count());
  }

  // Use lower 32 bits of channel koid as trace ID.
  {
    zx_info_handle_basic_t info;
    zx_object_get_info(device_.channel().get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                       nullptr);
    trace_id_ = info.koid & 0xffffffff;
    auto result = device_.SetTraceId(trace_id_);
    if (result.status() != ZX_OK)
      return log_err(status, "failed to set trace ID", name_);
  }

  return true;
}

zx::event FdioHidDecoder::GetEvent() {
  zx::event event;
  auto result = device_.GetReportsEvent();
  if (result.status() != ZX_OK) {
    log_err(result.status(), "event handle", name_);
    return event;
  }
  if (result->status != ZX_OK) {
    log_err(result->status, "event handle", name_);
    return event;
  }

  event = std::move(result->event);
  return event;
}

const std::vector<uint8_t>& FdioHidDecoder::ReadReportDescriptor(int* bytes_read) {
  *bytes_read = report_descriptor_.size();
  return report_descriptor_;
}

zx_status_t FdioHidDecoder::Read(uint8_t* data, size_t data_size, size_t* report_size,
                                 zx_time_t* timestamp) {
  auto result = device_.ReadReport();
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }

  if (result->data.count() > data_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(data, result->data.data(), result->data.count());
  *report_size = result->data.count();
  *timestamp = result->time;

  return ZX_OK;
}

zx_status_t FdioHidDecoder::Send(ReportType type, uint8_t report_id,
                                 const std::vector<uint8_t>& report) {
  FXL_CHECK(type != ReportType::INPUT);

  llcpp::fuchsia::hardware::input::ReportType fidl_report_type;
  switch (type) {
    case ReportType::INPUT:
      fidl_report_type = llcpp::fuchsia::hardware::input::ReportType::INPUT;
      break;
    case ReportType::OUTPUT:
      fidl_report_type = llcpp::fuchsia::hardware::input::ReportType::OUTPUT;
      break;
    case ReportType::FEATURE:
      fidl_report_type = llcpp::fuchsia::hardware::input::ReportType::FEATURE;
      break;
  }

  // This is a little unwieldy but we need a non-const version of `report` to make
  // the FIDL call, so we have to make a copy.
  std::vector<uint8_t> report_copy = report;
  fidl::VectorView<uint8_t> report_view(fidl::unowned_ptr<uint8_t>(report_copy.data()),
                                        report_copy.size());

  auto result = device_.SetReport(fidl_report_type, report_id, std::move(report_view));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->status != ZX_OK) {
    return result->status;
  }
  return ZX_OK;
}

zx_status_t FdioHidDecoder::GetReport(ReportType type, uint8_t report_id,
                                      std::vector<uint8_t>* report) {
  llcpp::fuchsia::hardware::input::ReportType real_type;
  switch (type) {
    case ReportType::INPUT:
      real_type = llcpp::fuchsia::hardware::input::ReportType::INPUT;
      break;
    case ReportType::OUTPUT:
      real_type = llcpp::fuchsia::hardware::input::ReportType::OUTPUT;
      break;
    case ReportType::FEATURE:
      real_type = llcpp::fuchsia::hardware::input::ReportType::FEATURE;
      break;
  }

  auto result = device_.GetReport(real_type, report_id);
  if (result.status() != ZX_OK) {
    FXL_LOG(ERROR) << "hid: get report failed " << zx_status_get_string(result.status());
    return result.status();
  }
  if (result->status != ZX_OK) {
    FXL_LOG(ERROR) << "hid: could not get report (id " << report_id << " type "
                   << static_cast<uint8_t>(real_type)
                   << ") (status=" << zx_status_get_string(result->status) << ")";
    return result->status;
  }

  report->resize(result->report.count());
  memcpy(report->data(), result->report.data(), result->report.count());

  return ZX_OK;
}

}  // namespace ui_input
