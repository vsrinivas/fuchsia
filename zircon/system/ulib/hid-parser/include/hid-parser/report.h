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

// Extracts |value_out| from |report| and ensures that is in the units
// specified by |attr|. This is the recommended extraction function for
// users of this library.
bool ExtractAsUnit(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                   double* value_out);
// Inserts |value_in| into |report| after it has been translated into
// the logical units described by |attr|.
bool InsertAsUnit(uint8_t* report, size_t report_len, const hid::Attributes& attr, double value_in);

// Extracts |value_out| from |report| and converts it into the units
// specified by |unit_out|.
bool ExtractWithUnit(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                     const Unit& unit_out, double* value_out);
// Given |value_in| with units |unit_in|, it converts it to |attr.unit| and
// then inserts it into |report|.
bool InsertWithUnit(uint8_t* report, size_t report_len, const hid::Attributes& attr,
                    const Unit& unit_in, double value_in);

// Helper functions that extracts the raw byte data from a report. This is only
// recommended for users that know what they are doing and are willing to
// use raw data or do their own conversion between logical and physical values.
bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 uint8_t* value_out);
bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 uint16_t* value_out);
bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 uint32_t* value_out);

bool InsertUint(uint8_t* report, size_t report_len, const hid::Attributes& attr, uint32_t value_in);

} // namespace hid

#endif // HID_PARSER_REPORT_H_
