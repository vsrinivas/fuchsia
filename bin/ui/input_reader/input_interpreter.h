// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_
#define GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

#include <hid/acer12.h>
#include <zircon/device/input.h>
#include <zircon/types.h>
#include <zx/event.h>

#include <map>
#include <string>

#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ui/input/fidl/input_reports.fidl.h"

namespace mozart {
namespace input {

class InputInterpreter {
 public:
  enum ReportType {
    kKeyboard,
    kMouse,
    kStylus,
    kTouchscreen,
  };

  using OnReportCallback = std::function<void(ReportType type)>;

  static std::unique_ptr<InputInterpreter>
  Open(int dirfd, std::string filename, mozart::InputDeviceRegistry* registry);
  ~InputInterpreter();

  bool Initialize();
  bool Read(bool discard);

  const std::string& name() const { return name_; }
  zx_handle_t handle() { return event_.get(); }

 private:
  enum class TouchDeviceType {
    NONE,
    ACER12,
    PARADISE,
    SAMSUNG,
  };

  InputInterpreter(std::string name,
                   int fd,
                   mozart::InputDeviceRegistry* registry);

  zx_status_t GetProtocol(int* out_proto);
  zx_status_t GetReportDescriptionLength(size_t* out_report_desc_len);
  zx_status_t GetReportDescription(uint8_t* out_buf,
                                   size_t out_report_desc_len);
  zx_status_t GetMaxReportLength(input_report_size_t* out_max_report_len);

  void NotifyRegistry();

  void ParseKeyboardReport(uint8_t* report, size_t len);
  void ParseMouseReport(uint8_t* report, size_t len);
  bool ParseAcer12TouchscreenReport(uint8_t* report, size_t len);
  bool ParseAcer12StylusReport(uint8_t* report, size_t len);
  bool ParseSamsungTouchscreenReport(uint8_t* report, size_t len);
  bool ParseParadiseTouchscreenReport(uint8_t* report, size_t len);

  const int fd_;
  const std::string name_;
  mozart::InputDeviceRegistry* registry_;

  zx::event event_;
  std::vector<uint8_t> report_;
  input_report_size_t max_report_len_ = 0;

  acer12_touch_t acer12_touch_reports_[2];

  bool has_keyboard_ = false;
  mozart::KeyboardDescriptorPtr keyboard_descriptor_;
  bool has_mouse_ = false;
  mozart::MouseDescriptorPtr mouse_descriptor_;
  bool has_stylus_ = false;
  mozart::StylusDescriptorPtr stylus_descriptor_;
  bool has_touchscreen_ = false;
  mozart::TouchscreenDescriptorPtr touchscreen_descriptor_;

  TouchDeviceType touch_device_type_ = TouchDeviceType::NONE;

  mozart::InputReportPtr keyboard_report_;
  mozart::InputReportPtr mouse_report_;
  mozart::InputReportPtr touchscreen_report_;
  mozart::InputReportPtr stylus_report_;

  mozart::InputDevicePtr input_device_;
};

}  // namespace input
}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_
