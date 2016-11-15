// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_READER_INPUT_DEVICE_H_
#define APPS_MOZART_SRC_INPUT_READER_INPUT_DEVICE_H_

#include <hid/acer12.h>
#include <magenta/device/input.h>
#include <magenta/types.h>
#include <mx/event.h>

#include <map>
#include <string>

#include "apps/mozart/src/input_reader/input_descriptor.h"
#include "apps/mozart/src/input_reader/input_report.h"

namespace mozart {
namespace input {

struct InputReport;
struct KeyboardReport;
struct MouseReport;
struct TouchReport;
struct StylusReport;

using OnReportCallback = std::function<void(InputReport::ReportType type)>;

class InputDevice {
 public:
  static std::unique_ptr<InputDevice> Open(int dirfd, std::string filename);
  ~InputDevice();

  bool Initialize();
  bool Read(const OnReportCallback& callback);

  const std::string& name() const { return name_; }
  mx_handle_t handle() { return event_.get(); }

  bool has_keyboard() const { return has_keyboard_; }
  bool has_mouse() const { return has_mouse_; }
  bool has_stylus() const { return has_stylus_; }
  bool has_touchscreen() const { return has_touchscreen_; }

  const KeyboardDescriptor& keyboard_descriptor() const {
    return keyboard_descriptor_;
  }
  const MouseDescriptor& mouse_descriptor() const { return mouse_descriptor_; }
  const StylusDescriptor& stylus_descriptor() const {
    return stylus_descriptor_;
  }
  const TouchscreenDescriptor& touchscreen_descriptor() const {
    return touchscreen_descriptor_;
  }

  const KeyboardReport& keyboard_report() const { return keyboard_report_; }
  const MouseReport& mouse_report() const { return mouse_report_; }
  const StylusReport& stylus_report() const { return stylus_report_; }
  const TouchReport& touch_report() const { return touch_report_; }

 private:
  InputDevice(std::string name, int fd);

  mx_status_t GetProtocol(int* out_proto);
  mx_status_t GetReportDescriptionLength(size_t* out_report_desc_len);
  mx_status_t GetReportDescription(uint8_t* out_buf,
                                   size_t out_report_desc_len);
  mx_status_t GetMaxReportLength(input_report_size_t* out_max_report_len);

  void ParseKeyboardReport(uint8_t* report, size_t len);
  void ParseMouseReport(uint8_t* report, size_t len);
  void ParseTouchscreenReport(uint8_t* report, size_t len);
  void ParseStylusReport(uint8_t* r, size_t len);

  const int fd_;
  const std::string name_;
  mx::event event_;
  std::vector<uint8_t> report_;
  input_report_size_t max_report_len_ = 0;

  acer12_touch_t acer12_touch_reports_[2];

  bool has_keyboard_ = false;
  KeyboardDescriptor keyboard_descriptor_;
  bool has_mouse_ = false;
  MouseDescriptor mouse_descriptor_;
  bool has_stylus_ = false;
  StylusDescriptor stylus_descriptor_;
  bool has_touchscreen_ = false;
  TouchscreenDescriptor touchscreen_descriptor_;

  KeyboardReport keyboard_report_;
  MouseReport mouse_report_;
  TouchReport touch_report_;
  StylusReport stylus_report_;
};

}  // namespace input
}  // namespace mozart

#endif  // APPS_MOZART_SRC_INPUT_READER_INPUT_DEVICE_H_
