// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_PARSER_PARSER_H_
#define HID_PARSER_PARSER_H_

#include <stdint.h>
#include <stdlib.h>

namespace hid {

// The hid report descriptor parser consists of a single function
// ParseReportDescriptor() that takes as input a USB report descriptor
// byte stream and on success returns a heap-allocated DeviceDescriptor
// structure. When not needed it can be freed via FreeDeviceDescriptor().
//
// The DeviceDescriptor data is organized at the first level by the
// three arrays which correspond to the feature fields for the input,
// output, and feature reports. Input, output, and feature reports each
// have their own length and fields: they are logically connected only
// because they share a report_id. The arrays of report features are of
// length {type}_count. Eg: the input_fields array has input_size ReportField
// structures.
//
// report[0].input_fields --->  ReportField
//                              +report_id
//                              +field_type == kInput
//                              +attr
//                              +col -------> Collection
//                                            +type
//                                            +parent ---> Collection
//
// The structure describes all the information returned by the device;
// no information present in the original stream is lost.
//
// The attr field of the ReportField contains all information to parse
// a report sent by the device. The ExtractUint functions will use the
// offset in the attribute to extract the necessary data.
//
// An example will enlighten. Assume |report| array as follows,
// with most fields omitted for brevity:
//
//    report[0]
//    .input_fields---> [0] report_id:      1
//                          usage           button,1
//                          flags           data,var
//                          bit_sz          1
//                          // For descriptors that have more than 1 report,
//                          // the first 8 bits are always the report id
//                          offset          8
//
//                      [1] report_id:      1
//                          usage           button,2
//                          flags           data,var
//                          bit_sz          1
//                          offset          9
//
//                      [2] report_id:      1
//                          usage           button,none
//                          flags           const
//                          bit_sz          6
//                          offset          10
//    report[1]
//    .input_fields---> [3] report_id:      3
//                          usage           desktop,X
//                          flags           data,var
//                          bit_sz          8
//                          offset          8
//
//                      [4] report_id:      3
//                          usage           desktop,Y
//                          flags           data,var
//                          bit_sz          8
//                          offset          16
//    report[2]
//    .input_fields---> [5] report_id:      4
//                          usage           desktop,wheel
//                          flags           data,var
//                          bit_sz          5
//                          offset          8
//
//                      [6] report_id:      4
//                          usage           desktop,none
//                          flags           const
//                          bit_sz          3
//                          offset          13
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
// Input, Output, and Feature reports are all sent over unique channels, which
// is how they are distinguished. This is important because they can share the
// same report id.

// Logical minimum and maximum per hid spec.
struct MinMax {
    int64_t min;
    int64_t max;
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
    uint32_t usage;
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
    // Indicates if field can be modified. Constant often means is padding.
    kData = 1 << 0,
    kConstant = 1 << 1,
    // The field is either an array or scalar. If it is an array only
    // the kData|kConstant and kAbsolute|kRelative flags are valid.
    kArray = 1 << 2,
    kScalar = 1 << 3,
    // Value is absolute wrt to a fixed origin or not.
    kAbsolute = 1 << 4,
    kRelative = 1 << 5,
    // Whether the data rolls over wrt to the logical min/max.
    kNoWrap = 1 << 6,
    kWrap = 1 << 7,
    // Data has been pre-processed, for example dead-zone.
    kLinear = 1 << 8,
    kNonLinear = 1 << 9,
    // Value returns to a preset value when the user is not interacting with control.
    kPreferredState = 1 << 10,
    kNoPreferred = 1 << 11,
    // If the control can enter a state when it does not report data.
    kNoNullPosition = 1 << 12,
    kNullState = 1 << 13,
    // Output-only: can the value be modified without host interaction.
    kNonVolatile = 1 << 14,
    kVolatile = 1 << 15,
    // Data is a fixed size stream.
    kBitField = 1 << 16,
    kBufferedBytes = 1 << 17,
};

// TODO(cpu): consider repurposing the kData| kArray | kNonLinear to indicate
// an array field which requires a lookup table. See adafruit trinket report id 4
// for an example of this case.

struct Collection {
    CollectionType type;
    Usage usage;
    Collection* parent; // Enclosing collection or null.
};

struct Attributes {
    Usage usage;
    Unit unit;
    MinMax logc_mm;
    MinMax phys_mm;
    uint8_t bit_sz;
    uint32_t offset;
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

    // The byte size includes the 1 byte for the report ID if the report ID is
    // not equal to zero.
    size_t input_byte_sz;
    size_t input_count;
    ReportField* input_fields;

    size_t output_byte_sz;
    size_t output_count;
    ReportField* output_fields;

    size_t feature_byte_sz;
    size_t feature_count;
    ReportField* feature_fields;
};

struct DeviceDescriptor {
    size_t rep_count;
    ReportDescriptor report[];
};

enum ParseResult : uint32_t {
    kParseOk = 0,
    kParseNoMemory = 1,
    kParseMoreNeeded = 2,
    kParseUnsuported = 3,
    kParseInvalidTag = 4,
    kParseInvalidItemType = 5,
    kParseInvalidItemValue = 6,
    kParseUsageLimit = 7,
    kParseInvalidRange = 8,
    kParseOverflow = 9,
    kParseLeftovers = 10,
    kParseUnexpectedCol = 11,
    kParseUnexpectedItem = 12,
    kParseInvalidUsage = 13,
    kParseMissingUsage = 14,
    kParserMissingPage = 15,
    kParserUnexpectedPop = 16,
    kParserInvalidID = 17
};

ParseResult ParseReportDescriptor(
    const uint8_t* rpt_desc, size_t desc_len,
    DeviceDescriptor** dev_desc);

void FreeDeviceDescriptor(DeviceDescriptor* dev_desc);

Collection* GetAppCollection(const ReportField* field);

// Helper for creating Usage constants.
template <typename P, typename U>
constexpr Usage USAGE(P page, U usage) {
    return Usage{static_cast<uint16_t>(page), static_cast<uint32_t>(usage)};
}

} // namespace hid

inline bool operator==(hid::Usage a, hid::Usage b) {
    return (a.page == b.page) && (a.usage == b.usage);
}

inline bool operator!=(hid::Usage a, hid::Usage b) {
    return (a.page != b.page) || (a.usage != b.usage);
}

inline bool operator==(hid::MinMax a, hid::MinMax b) {
    return (a.min == b.min) && (a.max == b.max);
}

#endif // HID_PARSER_PARSER_H_
