// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_INPUT_REPORT_FIDL_H_
#define HID_INPUT_REPORT_FIDL_H_

#include <fuchsia/input/report/llcpp/fidl.h>

#include <hid-input-report/descriptors.h>

namespace hid_input_report {

struct FidlMouseDesc {
  FidlMouseDesc() {}
  ::llcpp::fuchsia::input::report::MouseDescriptor mouse_descriptor;
  ::llcpp::fuchsia::input::report::MouseDescriptor::Builder mouse_builder =
      ::llcpp::fuchsia::input::report::MouseDescriptor::Build();
  ::llcpp::fuchsia::input::report::Axis movement_x;
  ::llcpp::fuchsia::input::report::Axis movement_y;
  ::llcpp::fuchsia::input::report::Axis scroll_v;
  ::llcpp::fuchsia::input::report::Axis scroll_h;
  fidl::VectorView<uint8_t> buttons_view;
  uint8_t buttons[hid_input_report::kMouseMaxButtons];
};

struct FidlDescriptor {
  FidlDescriptor() {}
  ::llcpp::fuchsia::input::report::DeviceDescriptor::Builder descriptor =
      ::llcpp::fuchsia::input::report::DeviceDescriptor::Build();

  FidlMouseDesc mouse_desc;
};

struct FidlMouseReport {
  FidlMouseReport() {}
  ::llcpp::fuchsia::input::report::MouseReport mouse_report;
  ::llcpp::fuchsia::input::report::MouseReport::Builder mouse_builder =
      ::llcpp::fuchsia::input::report::MouseReport::Build();
  fidl::VectorView<uint8_t> buttons_view;
};

// |Report| stores all of the metadata for the FIDL table for an InputReport.
// Each |Report| will have a corresponding |hid_input_report::Report| which
// stores the actual data.
struct FidlReport {
  FidlReport() {}
  ::llcpp::fuchsia::input::report::InputReport::Builder report =
      ::llcpp::fuchsia::input::report::InputReport::Build();
  union {
    FidlMouseReport mouse_report;
  };
};

zx_status_t SetMouseDescriptor(const hid_input_report::ReportDescriptor& hid_desc,
                               FidlDescriptor* descriptor);

// Sets up the FIDL table in |Report| to point to all of the values in |hid_report|.
// It would be nice if |hid_report| could be const, but the FIDL table needs to
// point to non-const values.
// |report| should have the same lifetime as |hid_report| since it will be pointing
// to the data in the |hid_report| struct.
zx_status_t SetMouseReport(hid_input_report::Report* hid_report, FidlReport* report);

}  // namespace hid_input_report

#endif  // HID_INPUT_REPORT_FIDL_H_
