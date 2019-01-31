// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdlib.h>

namespace hid {

// Value class that represents a USB HID report descriptor "Item" as defined
// by the Device Class Definition for Human Interface Devices Firmware
// specification 1.11 for "short items" (see usb.org).

class Item {
public:
    enum class Type : uint8_t {
        kMain,
        kGlobal,
        kLocal,
        kReserved,
        // Note: long items are not fully parsed.
        kLongItem,
    };

    enum class Tag: uint8_t {
        // Main tags.
        kInput,
        kOutput,
        kFeature,
        kCollection,
        kEndCollection,

        // Global tags.
        kUsagePage,
        kLogicalMinimum,
        kLogicalMaximum,
        kPhysicalMinimum,
        kPhysicalMaximum,
        kUnitExponent,
        kUnit,
        kReportSize,
        kReportId,
        kReportCount,
        kPush,
        kPop,

        // Local tags.
        kUsage,
        kUsageMinimum,
        kUsageMaximum,
        kDesignatorIndex,
        kDesignatorMinimum,
        kDesignatorMaximum,
        kStringIndex,
        kStringMinimum,
        kStringMaximum,
        kDelimiter,

        // Reserved tag (for any type).
        kReserved,
    };

    // Construct an Item from a HID report descriptor bytestream.
    // |data| should contain a hid report descriptor with |len| > 0
    // and the returned object is one parsed hid report item.
    //
    // Upon return |*actual| contains how many bytes to advance to the
    // next Item. Caller must check that:
    //  |*actual| is not zero
    //     If so, parsing is not supported for this stream.
    //  |*actual| is not greater than |len|.
    //    If so, more data is needed and returned item's data()
    //    is set to zero.
    //
    // Most bit patterns are valid items so if garbage data is given
    // to this method a set of valid-looking items can be returned so
    // the next layer must validate that the sequence of items is
    // reasonable and structurally correct.
    static Item ReadNext(const uint8_t* data, size_t len, size_t* actual);

    // Construct an Item from explicit values.
    // No validation is performed.
    constexpr Item(Type type, Tag tag, uint8_t size, uint32_t data)
        : type_(type), tag_(tag), size_(size), data_(data) {
    }

    Type type() const { return type_; }
    Tag tag() const { return tag_; }
    uint32_t data() const { return data_; }
    int32_t signed_data() const;

private:
    const Type type_;
    const Tag tag_;
    const uint8_t size_;
    const uint32_t data_;
};

}  // namespace hid
