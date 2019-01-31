// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-parser/item.h"

#include <string.h>
#include <zircon/assert.h>

namespace hid {
namespace impl {

Item::Tag get_main_tag(uint8_t b_tag) {
    switch (b_tag) {
    case 8: return Item::Tag::kInput;
    case 9: return Item::Tag::kOutput;
    case 10: return Item::Tag::kCollection;
    case 11: return Item::Tag::kFeature;
    case 12: return Item::Tag::kEndCollection;
    default: return Item::Tag::kReserved;
    }
}

Item::Tag get_global_tag(uint8_t b_tag) {
    switch (b_tag) {
    case 0: return Item::Tag::kUsagePage;
    case 1: return Item::Tag::kLogicalMinimum;
    case 2: return Item::Tag::kLogicalMaximum;
    case 3: return Item::Tag::kPhysicalMinimum;
    case 4: return Item::Tag::kPhysicalMaximum;
    case 5: return Item::Tag::kUnitExponent;
    case 6: return Item::Tag::kUnit;
    case 7: return Item::Tag::kReportSize;
    case 8: return Item::Tag::kReportId;
    case 9: return Item::Tag::kReportCount;
    case 10: return Item::Tag::kPush;
    case 11: return Item::Tag::kPop;
    default: return Item::Tag::kReserved;
    }
}

Item::Tag get_local_tag(uint8_t b_tag) {
    switch (b_tag) {
    case 0: return Item::Tag::kUsage;
    case 1: return Item::Tag::kUsageMinimum;
    case 2: return Item::Tag::kUsageMaximum;
    case 3: return Item::Tag::kDesignatorIndex;
    case 4: return Item::Tag::kDesignatorMinimum;
    case 5: return Item::Tag::kDesignatorMaximum;
    // No tag for 6.
    case 7: return Item::Tag::kStringIndex;
    case 8: return Item::Tag::kStringMinimum;
    case 9: return Item::Tag::kStringMaximum;
    case 10: return Item::Tag::kDelimiter;
    default: return Item::Tag::kReserved;
    }
}

// This is the bit pattern for long items which this
// library does not support.
constexpr uint8_t kLongItemMarker = 0xfe;

Item::Type get_type_and_size(uint8_t data, uint8_t* size) {
    if (data == kLongItemMarker)
        return Item::Type::kLongItem;

    // Short item.
    // Payload size is 0,1,2,4 bytes.
    auto b_size = static_cast<uint8_t>(data & 0x03);
    *size = ( b_size != 3) ? b_size : 4;

    switch ((data >> 2) & 0x03) {
    case 0: return Item::Type::kMain;
    case 1: return Item::Type::kGlobal;
    case 2: return Item::Type::kLocal;
    default: return Item::Type::kReserved;
    }
}

Item::Tag get_tag(Item::Type type, uint8_t data) {
    uint8_t b_tag = (data >> 4) & 0x0f;
    switch (type) {
    case Item::Type::kMain:     return get_main_tag(b_tag);
    case Item::Type::kGlobal:   return get_global_tag(b_tag);
    case Item::Type::kLocal:    return get_local_tag(b_tag);
    default: return Item::Tag::kReserved;
    }
}

}  // namespace impl.

Item Item::ReadNext(const uint8_t* data, size_t len, size_t* actual) {
    ZX_DEBUG_ASSERT(len != 0);

    uint8_t size = 0;

    auto type = impl::get_type_and_size(data[0], &size);
    auto tag = impl::get_tag(type, data[0]);

    // Amount to parse is 1-byte for the header and  |size| for the payload
    // for short items. Long items are not supported.
    *actual = (type != Item::Type::kLongItem) ? 1 + size : 0;

    uint32_t item_data = 0;
    if (*actual <= len) {
        for (uint8_t ix = 0; ix < size; ++ix) {
            item_data |= data[1 + ix] << (8 * ix);
        }
    }

    return Item(type, tag, size, item_data);
}

// Type punning beyond the magic 'char' type is no longer tolerated
// for example the simpler  "return *reinterpret_cast<T*>(data);"
// generates a warning even on gcc.
template <typename T>
T bit_cast(const uint32_t* data) {
    T dest;
    memcpy(&dest, data, sizeof(dest));
    return dest;
}

int32_t Item::signed_data() const {
    switch (size_) {
    case 1: return bit_cast<int8_t>(&data_);
    case 2: return bit_cast<int16_t>(&data_);
    case 4: return bit_cast<int32_t>(&data_);
    default: return 0;
    }
}

}  // namespace hid
