// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_PARSER_REPORT_H_
#define HID_PARSER_REPORT_H_

#include <stdint.h>
#include <stdlib.h>

#include <hid-parser/parser.h>
#include <hid-parser/units.h>

namespace hid {
struct Report {
    const uint8_t* data;
    size_t len;
};

// Extracts the data from report that has been translated
// into the logical units described by attr.unit. This is the recommended
// extraction function for users of this library.
bool ExtractAsUnit(const Report& report, const hid::Attributes& attr, double& value_out);

// Extracts the data already converted as a given unit
bool ExtractWithUnit(const Report& report, const hid::Attributes& attr,
                     double& value_out, const Unit& unit_out);

// Helper functions that extracts the raw byte data from a report. This is only
// recommended for users that know what they are doing and are willing to
// use raw data or do their own conversion between logical and physical values.
bool ExtractUint(const Report& report, const hid::Attributes& attr, uint8_t* value_out);
bool ExtractUint(const Report& report, const hid::Attributes& attr, uint16_t* value_out);
bool ExtractUint(const Report& report, const hid::Attributes& attr, uint32_t* value_out);

} // namespace hid

#endif // HID_PARSER_REPORT_H_
