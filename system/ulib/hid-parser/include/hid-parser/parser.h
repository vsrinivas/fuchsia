// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>

namespace hid {

// The hid report descriptor parser consists of a single function
// ParseReportDescriptor() that takes as input a USB report descriptor
// byte stream and on success returns a heap-allocated DeviceDescriptor
// structure. When not needed it can be freed by standard C++ delete.
//
// The DeviceDescriptor data is organized at the first level by the
// array |report[rep_count]| in which each entry points to the first
// field of each report and how many fields are expected on each report.
//
// report[0].first_field --->  ReportField
//                              +report_id
//                              +field_type
//                              +col -------> Collection
//                                            +type
//                                            +parent ---> Collection
//                             1
// The structure describes all the information returned by the device;
// no information present in the original stream is lost.
//
// When using it to parse reports sent by the device, two scenarios
// are important:
//   1 -  |rep_count| is 1 and report[0]->report_id is 0:
//      in this case the reports don't have a report id tag and the
//      first byte in the stream contains the first field.
//   2 - report[0]->report_id is not 0:
//      in this case each report first byte is the report_id tag
//      and must be matched to properly decode the report.
//
// Once the right starting ReportField has been matched then extracting
// each field is done by inspecting bit_sz and flags and using |next|
// to move to the following field. Reaching |next| == null means the
// end of the report.
//
// An example will enlighten. Assume |report| array as follows,
// with most fields omitted for brevity:
//
//    report[0]
//    .first_field--> [0] report_id:      1
//                        usage           button,1
//                        flags           data,var
//                        bit_sz          1
//
//                    [1] report_id:      1
//                        usage           button,2
//                        flags           data,var
//                        bit_sz          1
//
//                    [2] report_id:      1
//                        usage           button,none
//                        flags           const
//                        bit_sz          6
//    report[1]
//    .first_field--> [3] report_id:      3
//                        usage           desktop,X
//                        flags           data,var
//                        bit_sz          8
//
//                    [4] report_id:      3
//                        usage           desktop,Y
//                        flags           data,var
//                        bit_sz          8
//    report[2]
//    .first_field--> [5] report_id:      4
//                        usage           desktop,wheel
//                        flags           data,var
//                        bit_sz          5
//
//                    [6] report_id:      4
//                        usage           desktop,none
//                        flags           const
//                        bit_sz          3
//
// Now given the following report stream, with byte-order
// left to right:
//
//   03 b4 67 01 02 04 13 03 b5 6a
//   ------>--------->----------->
//
//  Can be parsed as the following 4 reports:
//
//  - 03 is report id, so Node [3] and Node [4] are in play
//       b4 is X-coord/desktop  (8-bits)
//       67 is Y-coord/desktop  (8-bits)
//
//  - 01 is report id, so Node [0], Node [1] and Node [3] are in play
//       02 is split into bits
//          0 is 1/button (1-bit)
//          1 is 2/button (1-bit)
//          0 is none/button (7-bit)       padding
//
//  - 04 is report id, so Node [5] and Node [6] are in play
//       13 is split into bits
//          13 is desktop/wheel (5-bit)
//           0 is desktop/none  (3-bit)    padding
//
//  - 03 is report id, so Node [3] and Node [4] are in play
//       b5 is X-coord/desktop  (8-bits)
//       6a is Y-coord/desktop  (8-bits)
//
//

// Logical minimum and maximum per hid spec.
struct MinMax {
    int32_t min;
    int32_t max;
};

// Physical units descriptor.
struct Unit {
    uint32_t type;
    int32_t exp;
};

// Describes the semantic meaning of fields. See the "HID Usage tables"
// document from usb.org.
struct Usage {
    uint16_t page;
    uint16_t usage;
};

enum class CollectionType : uint32_t {
    kPhysical,
    kApplication,
    kLogical,
    kReport,
    kNamedArray,
    kUsageSwitch,
    kUsageModifier,
    kReserved,
    kVendor
};

enum NodeType : uint32_t {
    kInput,
    kOutput,
    kFeature
};

enum FieldTypeFlags : uint32_t {
    // Indicates if field can be modfied. Constant often means is padding.
    kData               = 1 << 0,
    kConstant           = 1 << 1,
    // The field is either an array or scalar. If it is an array only
    // the kData|kConstant and kAbsolute|kRelative flags are valid.
    kArray              = 1 << 2,
    kScalar             = 1 << 3,
    // Value is absolute wrt to a fixed origin or not.
    kAbsolute           = 1 << 4,
    kRelative           = 1 << 5,
    // Whether the data rolls over wrt to the logical min/max.
    kNoWrap             = 1 << 6,
    kWrap               = 1 << 7,
    // Data has been pre-processed, for example dead-zone.
    kLinear             = 1 << 8,
    kNonLinear          = 1 << 9,
    // Value returns to a preset value when the user is not interacting with control.
    kPreferredState     = 1 << 10,
    kNoPreferred        = 1 << 11,
    // If the control can enter a state when it does not report data.
    kNoNullPosition     = 1 << 12,
    kNullState          = 1 << 13,
    // Output-only: can the value be modified without host interaction.
    kNonVolatile        = 1 << 14,
    kVolatile           = 1 << 15,
    // Data is a fixed size stream.
    kBitField           = 1 << 16,
    kBufferedBytes      = 1 << 17,
};

// TODO(cpu): consider repurposing the kData| kArray | kNonLinear to indicate
// an array field which requires a lookup table. See adafruit trinket report id 4
// for an example of this case.

struct Collection {
    CollectionType type;
    Usage usage;
    Collection* parent;            // Enclosing collection or null.
};

struct Attributes {
    Usage usage;
    Unit unit;
    MinMax logc_mm;
    MinMax phys_mm;
    uint8_t bit_sz;
};

struct ReportField {
    uint8_t report_id;
    Attributes attr;
    NodeType type;
    uint32_t flags;
    Collection* col;
};

struct ReportDescriptor {
    uint8_t report_id;
    size_t count;
    ReportField* first_field;
};

struct DeviceDescriptor {
    size_t rep_count;
    ReportDescriptor report[];
};

enum ParseResult : uint32_t {
    kParseOk                 = 0,
    kParseNoMemory           = 1,
    kParseMoreNeeded         = 2,
    kParseUnsuported         = 3,
    kParseInvalidTag         = 4,
    kParseInvalidItemType    = 5,
    kParseInvalidItemValue   = 6,
    kParseUsageLimit         = 7,
    kParseInvalidRange       = 8,
    kParseOverflow           = 9,
    kParseLeftovers          = 10,
    kParseUnexpectedCol      = 11,
    kParseUnexectedItem      = 12,
    kParseInvalidUsage       = 13,
    kParseMissingUsage       = 14,
    kParserMissingPage       = 15,
    kParserUnexpectedPop     = 16,
    kParserInvalidID         = 17
};

ParseResult ParseReportDescriptor(
    const uint8_t* rpt_desc, size_t desc_len,
    DeviceDescriptor** dev_desc);

}  // namespace hid
