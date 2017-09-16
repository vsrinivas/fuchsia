// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This header contains elements that may be used to construct Human Interface
// Device (HID) descriptors, as defined by the USB Implementers Forum.
//
// The macros defined here expand to comma-separated byte values, and are
// suitable for use in array definitions.  E.g.
//
// // This is a HID Descriptor that defines a Mouse with no buttons and two
// // relative positional axes.
// uint8_t hid_descriptor[] = {
//     HID_USAGE_PAGE(1), // Generic Desktop
//     HID_USAGE(2),      // Mouse
//     HID_COLLECTION_APPLICATION,
//         HID_USAGE(1),  // Pointer
//         HID_COLLECTION_PHYSICAL,
//         HID_LOGICAL_MIN(-127),
//         HID_LOGICAL_MAX(-127),
//         HID_REPORT_SIZE(8),
//         HID_REPORT_COUNT(1),
//         HID_USAGE(0x30), // X
//         HID_INPUT(0x6), // Data Variable Relative
//         HID_USAGE(0x31), // Y
//         HID_INPUT(0x6), // Data Variable Relative
//         HID_END_COLLECTION,
//     HID_END_COLLECTION,
// };
//
// Future Work:
// - Define nice shorthands for the argument to Input/Output/Feature
// - Define commonly used usage pages and usages
// - Define units element and commonly used units
// - Support Long Items

#define _HID_LOW8(v) (unsigned char)(v)
#define _HID_SECOND8(v) (unsigned char)((v) >> 8)
#define _HID_THIRD8(v) (unsigned char)((v) >> 16)
#define _HID_HIGH8(v) (unsigned char)((v) >> 24)
#define _HID_MAIN_VAL(bTag, v) (((bTag) << 4) | 0x1), _HID_LOW8(v)
#define _HID_MAIN_VAL16(bTag, v) (((bTag) << 4) | 0x2), _HID_LOW8(v), _HID_SECOND8(v)

#define _HID_GLOBAL_VAL(bTag, v) (((bTag) << 4) | 0x5), _HID_LOW8(v)
#define _HID_GLOBAL_VAL16(bTag, v) (((bTag) << 4) | 0x6), _HID_LOW8(v), _HID_SECOND8(v)
#define _HID_GLOBAL_VAL32(bTag, v) (((bTag) << 4) | 0x7), _HID_LOW8(v), _HID_SECOND8(v), \
                                     _HID_THIRD8(v), _HID_HIGH8(v)

#define _HID_LOCAL_VAL(bTag, v) (((bTag) << 4) | 0x9), _HID_LOW8(v)
#define _HID_LOCAL_VAL16(bTag, v) (((bTag) << 4) | 0xa), _HID_LOW8(v), _HID_SECOND8(v)

// Main HID items
#define  HID_INPUT(v)        _HID_MAIN_VAL(0x8,    v)
#define  HID_INPUT16(v)      _HID_MAIN_VAL16(0x8,  v)
#define  HID_OUTPUT(v)       _HID_MAIN_VAL(0x9,    v)
#define  HID_OUTPUT16(v)     _HID_MAIN_VAL16(0x9,  v)
#define  HID_FEATURE(v)      _HID_MAIN_VAL(0xb,    v)
#define  HID_FEATURE16(v)    _HID_MAIN_VAL16(0xb,  v)
#define  HID_COLLECTION(v)   _HID_MAIN_VAL(0xa,    v)
#define  HID_END_COLLECTION  0xc0

#define  HID_COLLECTION_PHYSICAL        HID_COLLECTION(0)
#define  HID_COLLECTION_APPLICATION     HID_COLLECTION(1)
#define  HID_COLLECTION_LOGICAL         HID_COLLECTION(2)
#define  HID_COLLECTION_REPORT          HID_COLLECTION(3)
#define  HID_COLLECTION_NAMED_ARRAY     HID_COLLECTION(4)
#define  HID_COLLECTION_USAGE_SWITCH    HID_COLLECTION(5)
#define  HID_COLLECTION_USAGE_MODIFIER  HID_COLLECTION(6)

// Global HID items
#define  HID_USAGE_PAGE(v)      _HID_GLOBAL_VAL(0x0,    v)
#define  HID_USAGE_PAGE16(v)    _HID_GLOBAL_VAL16(0x0,  v)
#define  HID_LOGICAL_MIN(v)     _HID_GLOBAL_VAL(0x1,    v)
#define  HID_LOGICAL_MIN16(v)   _HID_GLOBAL_VAL16(0x1,  v)
#define  HID_LOGICAL_MIN32(v)   _HID_GLOBAL_VAL32(0x1,  v)
#define  HID_LOGICAL_MAX(v)     _HID_GLOBAL_VAL(0x2,    v)
#define  HID_LOGICAL_MAX16(v)   _HID_GLOBAL_VAL16(0x2,  v)
#define  HID_LOGICAL_MAX32(v)   _HID_GLOBAL_VAL32(0x2,  v)
#define  HID_PHYSICAL_MIN(v)    _HID_GLOBAL_VAL(0x3,    v)
#define  HID_PHYSICAL_MIN16(v)  _HID_GLOBAL_VAL16(0x3,  v)
#define  HID_PHYSICAL_MIN32(v)  _HID_GLOBAL_VAL32(0x3,  v)
#define  HID_PHYSICAL_MAX(v)    _HID_GLOBAL_VAL(0x4,    v)
#define  HID_PHYSICAL_MAX16(v)  _HID_GLOBAL_VAL16(0x4,  v)
#define  HID_PHYSICAL_MAX32(v)  _HID_GLOBAL_VAL32(0x4,  v)
#define  HID_UNIT_EXPONENT(v)   _HID_GLOBAL_VAL(0x5,    (v) & 0xf)
#define  HID_REPORT_SIZE(v)     _HID_GLOBAL_VAL(0x7,    v)
#define  HID_REPORT_ID(v)       _HID_GLOBAL_VAL(0x8,    v)
#define  HID_REPORT_COUNT(v)    _HID_GLOBAL_VAL(0x9,    v)
#define  HID_PUSH               0xa4
#define  HID_POP                0xb4

// Local HID items
#define  HID_USAGE(v)      _HID_LOCAL_VAL(0x0,    v)
#define  HID_USAGE16(v)    _HID_LOCAL_VAL16(0x0,  v)
#define  HID_USAGE_MIN(v)  _HID_LOCAL_VAL(0x1,    v)
#define  HID_USAGE_MAX(v)  _HID_LOCAL_VAL(0x2,    v)
