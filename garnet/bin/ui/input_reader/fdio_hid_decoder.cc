// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/fdio_hid_decoder.h"

#include <unistd.h>

#include <fbl/auto_call.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/input/c/fidl.h>
#include <hid/acer12.h>
#include <hid/egalax.h>
#include <hid/eyoyo.h>
#include <hid/ft3x27.h>
#include <hid/paradise.h>
#include <hid/samsung.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>
#include <lib/fzl/fdio.h>
#include <trace/event.h>
#include <zircon/status.h>

namespace {

#define HID_REPORT_TRACE_ID(trace_id, report_id) \
  (((uint64_t)(report_id) << 32) | (trace_id))

bool log_err(zx_status_t status, const std::string& what,
             const std::string& name) {
  FXL_LOG(ERROR) << "hid: could not get " << what << " from " << name
                 << " (status=" << zx_status_get_string(status) << ")";
  return false;
}

}  // namespace

namespace mozart {

FdioHidDecoder::FdioHidDecoder(const std::string& name, fbl::unique_fd fd)
    : fd_(std::move(fd)), name_(name) {}

FdioHidDecoder::~FdioHidDecoder() = default;

bool FdioHidDecoder::Init() {
  // |fzl::FdioCaller| expects full temporary ownership of the file
  // descriptor, but it doesn't actually require it. We still need the file
  // descriptor to set up some devices in |ParseProtocol| using C setup
  // functions. They do the same thing as |fzl::FdioCaller|, in particular
  // |fzl::FdioCaller::borrow_channel()|. They do both take references to the
  // corresponding |fdio_t|, but that is refcounted.
  //
  // Since this is really unsafe wrt. |fd_|, we need to be sure to relinquish
  // ownership by |caller| when we're done.
  fzl::FdioCaller caller(fbl::unique_fd(fd_.get()));
  auto auto_releaser = fbl::MakeAutoCall([&]() { caller.release().release(); });

  uint16_t max_len = 0;
  zx_status_t status = fuchsia_hardware_input_DeviceGetMaxInputReportSize(
      caller.borrow_channel(), &max_len);
  report_.resize(max_len);

  zx_handle_t svc = caller.borrow_channel();

  // Get the Boot Protocol if there is one.
  fuchsia_hardware_input_BootProtocol boot_protocol;
  status = fuchsia_hardware_input_DeviceGetBootProtocol(svc, &boot_protocol);
  if (status != ZX_OK) {
    return log_err(status, "ioctl protocol", name_);
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
  status =
      fuchsia_hardware_input_DeviceGetReportDescSize(svc, &report_desc_len);
  if (status != ZX_OK)
    return log_err(status, "report descriptor length", name_);

  report_descriptor_.resize(report_desc_len);
  size_t actual;
  status = fuchsia_hardware_input_DeviceGetReportDesc(
      svc, report_descriptor_.data(), report_descriptor_.size(), &actual);
  if (status != ZX_OK)
    return log_err(status, "report descriptor", name_);
  report_descriptor_.resize(actual);

  // Use lower 32 bits of channel koid as trace ID.
  zx_info_handle_basic_t info;
  zx_object_get_info(svc, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                     nullptr);
  trace_id_ = info.koid & 0xffffffff;
  status = fuchsia_hardware_input_DeviceSetTraceId(svc, trace_id_);
  if (status != ZX_OK)
    return log_err(status, "failed to set trace ID", name_);

  return true;
}

zx::event FdioHidDecoder::GetEvent() {
  zx::event event;

  // See comment in Init() about this pattern
  fzl::FdioCaller caller(fbl::unique_fd(fd_.get()));
  auto auto_releaser = fbl::MakeAutoCall([&]() { caller.release().release(); });

  zx_status_t call_status;
  zx_status_t status = fuchsia_device_ControllerGetEventHandle(
          caller.borrow_channel(), &call_status, event.reset_and_get_address());
  if (status == ZX_OK) {
      status = call_status;
  }
  if (status != ZX_OK) {
    log_err(status, "event handle", name_);
    return {};
  }
  return event;
}

void FdioHidDecoder::SetupDevice(Device device) {
  switch (device) {
    case Device::EYOYO:
      setup_eyoyo_touch(fd_.get());
      break;
    case Device::SAMSUNG:
      setup_samsung_touch(fd_.get());
      break;
    case Device::FT3X27:
      setup_ft3x27_touch(fd_.get());
      break;
    default:
      break;
  }
}

const std::vector<uint8_t>& FdioHidDecoder::ReadReportDescriptor(
    int* bytes_read) {
  *bytes_read = report_descriptor_.size();
  return report_descriptor_;
}

const std::vector<uint8_t>& FdioHidDecoder::Read(int* bytes_read) {
  *bytes_read = read(fd_.get(), report_.data(), report_.size());

  TRACE_FLOW_END("input", "hid_report",
                 HID_REPORT_TRACE_ID(trace_id_, reports_read_));
  ++reports_read_;

  return report_;
}

}  // namespace mozart
