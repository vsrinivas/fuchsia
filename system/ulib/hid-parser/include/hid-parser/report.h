// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_PARSER_REPORT_H_
#define HID_PARSER_REPORT_H_

#include <stdint.h>
#include <stdlib.h>

#include <hid-parser/parser.h>

namespace hid {
struct Report {
    const uint8_t* data;
    size_t len;
};

bool ExtractUint(const Report& report, const hid::Attributes& attr, uint8_t* value_out);
bool ExtractUint(const Report& report, const hid::Attributes& attr, uint16_t* value_out);
bool ExtractUint(const Report& report, const hid::Attributes& attr, uint32_t* value_out);

} // namespace hid

#endif  // HID_PARSER_REPORT_H_
