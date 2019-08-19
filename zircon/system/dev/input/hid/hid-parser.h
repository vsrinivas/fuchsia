// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_INPUT_HID_HID_PARSER_H_
#define ZIRCON_SYSTEM_DEV_INPUT_HID_HID_PARSER_H_

#include <stdint.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef uint8_t input_report_id_t;
typedef uint16_t input_report_size_t;

typedef struct hid_report_size {
  uint8_t id;
  input_report_size_t in_size;
  input_report_size_t out_size;
  input_report_size_t feat_size;
} hid_report_size_t;

typedef struct hid_reports {
  hid_report_size_t* sizes;
  size_t sizes_len;
  size_t num_reports;
  bool has_rpt_id;
} hid_reports_t;

zx_status_t hid_lib_parse_reports(const uint8_t* buf, const size_t buf_len, hid_reports_t* reports);

__END_CDECLS

#endif  // ZIRCON_SYSTEM_DEV_INPUT_HID_HID_PARSER_H_
