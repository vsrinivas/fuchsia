// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <hid-parser/item.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>

#include "hid-parser.h"

zx_status_t hid_lib_parse_reports(const uint8_t* buf, const size_t buf_len,
                                  hid_reports_t* reports) {
  hid::DeviceDescriptor* desc = nullptr;
  auto res = hid::ParseReportDescriptor(buf, buf_len, &desc);

  if (res != hid::ParseResult::kParseOk) {
    return -1;
  }

  for (size_t item = 0; item < desc->rep_count; item++) {
    hid::ReportDescriptor* desc_rep = &desc->report[item];
    hid_report_size_t* hiddev_rep = &reports->sizes[item];

    hiddev_rep->id = desc_rep->report_id;
    if (hiddev_rep->id != 0) {
      reports->has_rpt_id = true;
    }
    reports->num_reports++;

    hiddev_rep->in_size = static_cast<input_report_size_t>(desc_rep->input_byte_sz * 8);
    hiddev_rep->out_size = static_cast<input_report_size_t>(desc_rep->output_byte_sz * 8);
    hiddev_rep->feat_size = static_cast<input_report_size_t>(desc_rep->feature_byte_sz * 8);
  }

  FreeDeviceDescriptor(desc);
  return ZX_OK;
}
