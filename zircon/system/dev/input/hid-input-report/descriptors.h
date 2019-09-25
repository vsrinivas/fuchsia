// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_INPUT_HID_INPUT_REPORT_DESCRIPTORS_H_
#define ZIRCON_SYSTEM_DEV_INPUT_HID_INPUT_REPORT_DESCRIPTORS_H_

#include <fuchsia/input/report/llcpp/fidl.h>

#include <hid-input-report/descriptors.h>

namespace hid_input_report_dev {

namespace llcpp_report = ::llcpp::fuchsia::input::report;

struct MouseDesc {
  MouseDesc() {}
  llcpp_report::MouseDescriptor mouse_descriptor;
  llcpp_report::MouseDescriptor::Builder mouse_builder = llcpp_report::MouseDescriptor::Build();
  llcpp_report::Axis movement_x;
  llcpp_report::Axis movement_y;
  llcpp_report::Axis scroll_v;
  llcpp_report::Axis scroll_h;
  fidl::VectorView<uint8_t> buttons_view;
  uint8_t buttons[hid_input_report::kMouseMaxButtons];
};

struct Descriptor {
  Descriptor() {}
  llcpp_report::DeviceDescriptor::Builder descriptor = llcpp_report::DeviceDescriptor::Build();

  MouseDesc mouse_desc;
};

struct MouseReport {
  MouseReport() {}
  llcpp_report::MouseReport mouse_report;
  llcpp_report::MouseReport::Builder mouse_builder = llcpp_report::MouseReport::Build();
  fidl::VectorView<uint8_t> buttons_view;
};

// |Report| stores all of the metadata for the FIDL table for an InputReport.
// Each |Report| will have a corresponding |hid_input_report::Report| which
// stores the actual data.
struct Report {
  Report() {}
  llcpp_report::InputReport::Builder report = llcpp_report::InputReport::Build();
  union {
    MouseReport mouse_report;
  };
};

zx_status_t SetMouseDescriptor(const hid_input_report::ReportDescriptor& hid_desc,
                               Descriptor* descriptor);

// Sets up the FIDL table in |Report| to point to all of the values in |hid_report|.
// It would be nice if |hid_report| could be const, but the FIDL table needs to
// point to non-const values.
// |report| should have the same lifetime as |hid_report| since it will be pointing
// to the data in the |hid_report| struct.
zx_status_t SetMouseReport(hid_input_report::Report* hid_report, Report* report);

}  // namespace hid_input_report_dev

#endif  // ZIRCON_SYSTEM_DEV_INPUT_HID_INPUT_REPORT_DESCRIPTORS_H_
