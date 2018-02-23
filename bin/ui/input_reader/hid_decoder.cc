// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/hid_decoder.h"

#include <fcntl.h>
#include <unistd.h>

#include <zircon/device/device.h>
#include <zircon/device/input.h>

#include <hid/samsung.h>
#include <hid-parser/parser.h>

#include "lib/fxl/logging.h"

namespace {
bool log_if_err(ssize_t rc, const std::string& what, const std::string& name) {
  if (rc < 0) {
    FXL_LOG(ERROR) << "hid: could not get "<<  what << " from " << name
                   << " (status=" << rc << ")";
    return false;
  }
  return true;
}
}

namespace mozart {

HidDecoder::HidDecoder(const std::string& name, int fd)
    : fd_(fd), name_(name) {
}

bool HidDecoder::Init(int* out_proto) {
  ssize_t rc = ioctl_input_get_protocol(fd_, out_proto);
  if (rc < 0)
    return log_if_err(rc, "protocol", name_);

  input_report_size_t max_len = 0;
  rc = ioctl_input_get_max_reportsize(fd_, &max_len);
  report_.resize(max_len);

  return log_if_err(rc, "max report size", name_);
}

bool HidDecoder::GetReportDescriptionLength(size_t* out_report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc_size(fd_, out_report_desc_len);
  return log_if_err(rc, "report descriptor length", name_);
}

bool HidDecoder::GetReportDescription(uint8_t* out_buf, size_t out_report_desc_len) {
  ssize_t rc = ioctl_input_get_report_desc(fd_, out_buf, out_report_desc_len);
  return log_if_err(rc, "report descriptor", name_);
}

bool HidDecoder::GetEvent(zx_handle_t* handle) {
  ssize_t rc = ioctl_device_get_event_handle(fd_, handle);
  return log_if_err(rc, "event handle", name_);
}

void HidDecoder::apply_samsung_touch_hack() {
  setup_samsung_touch(fd_);
}

const std::vector<uint8_t>& HidDecoder::Read(int* bytes_read) {
  *bytes_read = read(fd_, report_.data(), report_.size());
  return report_;
}

}  // namespace mozart
