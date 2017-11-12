// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid-parser/parser.h>
#include <hid-parser/item.h>

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

namespace {
// Takes a every bit from 0 to 15 and converts them into a
// 01 if 1 or 10 if 0.
uint32_t expand_bitfield(uint32_t bitfield) {
    uint32_t result = 0u;
    for (uint8_t ix = 0; ix != 16; ++ix) {
        uint32_t twobit = (bitfield & 0x1) ? 0x10 : 0x01;
        result |= twobit << (2 * ix);
        bitfield >>= 1;
    }
    return result;
}
}  // namespace


namespace hid {
namespace impl {

// Parsing hid report descriptors is is complicated by the fact that
// there is a fair amount of flexibility on the format. In particular
// the format is to be understood as an opcode-based program that
// sets either some global or local state that at defined points is
// converted in a series of fields.
//
// The expected top level structure is:
//
//  App-Collection --> [Collection ... Collection] -->
//
// Followed by one or more [Input] [Output] [Feature] items, then
// followed by one or more end-collection items.
//
// This is however insufficient. Other items are interspersed that
// qualify the above items, both in purpose and in actual report
// layout.
//
// An example for the simplest mouse input report is instructive:
//
//  Collection-Application(Desktop, Mouse) {
//      Collection-Physical(Pointer) {
//          Input(1-bit, Data, Button, (1,0))
//          Input(1-bit, Data, Button, (1,0))
//          Input(1-bit, Data, Button, (1,0))
//          Input(5-bit, Padding, Button)
//          Input(8-bit, Data, X, (127,-127))
//          Input(8-bit, Data, Y, (127,-127))
//      }
//  }
//
//  All the qualifiers above inside "(" and ")" are opcodes
//  interspersed before the collection or input,output,feature
//  items. The system/utest/hid-parser has many examples captured
//  from real devices.


// Limit on the collection count we can process, complicated devices
// such as touch-pad are in the 10 to 20 range. This limitation can be
// removed with a bit more code if needed. To note, the other variable
// items such fields in a report are only limited by available memory.
constexpr size_t kMaxCollectionCount = 128u;

bool is_valid_collection(uint32_t col) {
    return (col <= static_cast<uint32_t>(CollectionType::kVendor));
}

bool is_app_collection(uint32_t col) {
    return (col == static_cast<uint32_t>(CollectionType::kApplication));
}

class ParseState {
public:
    ParseState()
        : usage_range_(),
          table_(),
          curr_col_ix_(-1) {
    }

    bool Init() {
        fbl::AllocChecker ac;
        coll_.reserve(kMaxCollectionCount, &ac);
        return ac.check();
    }

    ParseResult start_collection(uint32_t data) {                 // Main
        if (!is_valid_collection(data))
            return kParseInvalidItemValue;

        // By reserving and doing the size() check here, we ensure
        // never a re-alloc, keeping the intra-collection pointers valid.
        if (coll_.size() > kMaxCollectionCount)
            return kParseOverflow;

        Collection* parent = nullptr;

        if (curr_col_ix_ < 0) {
            // The first collection must be an application collection.
            if (!is_app_collection(data))
                return kParseUnexpectedCol;
        } else if (!is_app_collection(data)) {
            parent = &coll_[curr_col_ix_];
        }

        Collection col {
            static_cast<CollectionType>(data),
            table_.attributes.usage,
            parent,
            nullptr         // first report node. It will be
        };                  // wired during post processing.

        coll_.push_back(col);
        curr_col_ix_ = static_cast<int32_t>(coll_.size() - 1);
        return kParseOk;
    }

    ParseResult end_collection(uint32_t data) {
        if (data != 0u)
            return kParseInvalidItemValue;
        if (curr_col_ix_ < 0)
            return kParseInvalidTag;
        // We don't free collection items until post-processing.
        curr_col_ix_--;
        return kParseOk;
    }

    ParseResult add_field(NodeType type, uint32_t data) {       // Main
        if (coll_.size() == 0)
            return kParseUnexectedItem;

        for (uint32_t ix = 0; ix != table_.report_count; ++ ix) {

            auto curr_col = &coll_[coll_.size() - 1];

            ReportField field {
                table_.report_id,
                {},          // TODO(cpu): attributes!
                type,
                expand_bitfield(data),
                curr_col,
                nullptr     // next node. It will be wired
            };              // during post-processing.

            fbl::AllocChecker ac;
            fields_.push_back(field, &ac);
            if (!ac.check())
                return kParseNoMemory;

            // TOD(cpu): handle usages and sizes!
        }

        return kParseOk;
    }

    ParseResult reset_usage() {                                 //  after each Main
        // Is it an error to drop pending usages?
        usages_.reset();
        usage_range_ = {};
        return kParseOk;
    }

    ParseResult add_usage(uint32_t data) {                      // Local
        // TODO(cpu): support extended usages. Here
        // and on set_usage_min() and set_usage_max().
        if (data > UINT16_MAX)
            return kParseUnsuported;

        usages_.push_back(static_cast<uint16_t>(data));
        return kParseOk;
    }

    ParseResult set_usage_min(uint32_t data) {                  // Local
        if (data > UINT16_MAX)
            return kParseUnsuported;

        usage_range_.min = data;
        return kParseOk;
    }

    ParseResult set_usage_max(uint32_t data) {                  // Local
        if (data > UINT16_MAX)
            return kParseUnsuported;

        usage_range_.max = data;
        return kParseOk;
    }

    ParseResult set_usage_page(uint32_t data) {                 // Global
        if (data > UINT16_MAX)
            return kParseInvalidRange;

        table_.attributes.usage.page =
            static_cast<uint16_t>(data);
        return kParseOk;
    }

    ParseResult set_logical_min(uint32_t data) {                // Global
        table_.attributes.logc_mm.min = data;
        return kParseOk;
    }

    ParseResult set_logical_max(uint32_t data) {                // Global
        table_.attributes.logc_mm.max = data;
        return kParseOk;
    }

    ParseResult set_physical_min(uint32_t data) {                // Global
        table_.attributes.phys_mm.min = data;
        return kParseOk;
    }

    ParseResult set_physical_max(uint32_t data) {                // Global
        table_.attributes.phys_mm.max = data;
        return kParseOk;
    }

    ParseResult set_unit(uint32_t data) {
        // The unit parsing is a complicated per nibble
        // accumulation of units. Leave that to application.
        table_.attributes.unit.type = data;
        return kParseOk;
    }

    ParseResult set_unit_exp(uint32_t data) {                   // Global
        int32_t exp = static_cast<uint8_t>(data);
        // The exponent uses a weird, not fully specified
        // conversion, for example the value 0xf results
        // in -1 exponent. See USB HID spec doc.
        if (exp > 7)
            exp = exp - 16;
        table_.attributes.unit.exp = exp;
        return kParseOk;
    }

    ParseResult set_report_id(uint32_t data) {                  // Global
        if (data == 0)
            return kParserInvalidID;
        if (data > UINT8_MAX)
            return kParseInvalidRange;
        table_.report_id = static_cast<uint8_t>(data);
        return kParseOk;
    }

    ParseResult set_report_count(uint32_t data) {               // Global
        table_.report_count = data;
        return kParseOk;
    }

    ParseResult set_report_size(uint32_t data) {                // Global
        if (data > UINT8_MAX)
            return kParseInvalidRange;
        table_.bits_size = static_cast<uint8_t>(data);
        return kParseOk;
    }

    ParseResult push(uint32_t data) {                          // Global
        fbl::AllocChecker ac;
        stack_.push_back(table_, &ac);
        return ac.check() ? kParseOk : kParseNoMemory;
    }

    ParseResult pop(uint32_t data) {                           // Global
        if (stack_.size() == 0)
            return kParserUnexpectedPop;

        table_ = stack_[stack_.size() -1];
        stack_.pop_back();
        return kParseOk;
    }

private:
    struct StateTable {
        Attributes attributes;
        uint32_t report_count;
        uint8_t report_id;
        uint8_t bits_size;
    };

    // Internal state per spec:
    MinMax usage_range_;
    StateTable table_;
    fbl::Vector<StateTable> stack_;
    fbl::Vector<uint16_t> usages_;
    // Temporary output model:
    int32_t curr_col_ix_;
    fbl::Vector<Collection> coll_;
    fbl::Vector<ReportField> fields_;
};

ParseResult ProcessMainItem(const hid::Item& item, ParseState* state) {
    switch (item.tag()) {
    case Item::Tag::kInput:
    case Item::Tag::kOutput:
    case Item::Tag::kFeature:  // fall thru
        return state->add_field(static_cast<NodeType>(item.tag()), item.data());

    case Item::Tag::kCollection: return state->start_collection(item.data());
    case Item::Tag::kEndCollection: return state->end_collection(item.data());
    default: return kParseInvalidTag;
    }

    return state->reset_usage();
}

ParseResult ProcessGlobalItem(const hid::Item& item, ParseState* state) {
    switch (item.tag()) {
    case Item::Tag::kUsagePage: return state->set_usage_page(item.data());
    case Item::Tag::kLogicalMinimum: return state->set_logical_min(item.data());
    case Item::Tag::kLogicalMaximum: return state->set_logical_max(item.data());
    case Item::Tag::kPhysicalMinimum: return state->set_physical_min(item.data());
    case Item::Tag::kPhysicalMaximum: return state->set_physical_max(item.data());
    case Item::Tag::kUnitExponent: return state->set_unit_exp(item.data());
    case Item::Tag::kUnit: return state->set_unit(item.data());
    case Item::Tag::kReportSize: return state->set_report_size(item.data());
    case Item::Tag::kReportId: return state->set_report_id(item.data());
    case Item::Tag::kReportCount: return state->set_report_count(item.data());
    case Item::Tag::kPush: return state->push(item.data());
    case Item::Tag::kPop: return state->pop(item.data());
    default: return kParseInvalidTag;
    }
}

ParseResult ProcessLocalItem(const hid::Item& item, ParseState* state) {
    switch (item.tag()) {
    case Item::Tag::kUsage: return state->add_usage(item.data());
    case Item::Tag::kUsageMinimum: return state->set_usage_min(item.data());
    case Item::Tag::kUsageMaximum: return state->set_usage_max(item.data());

    case Item::Tag::kDesignatorIndex:  // Fall thru. TODO(cpu) implement.
    case Item::Tag::kDesignatorMinimum:
    case Item::Tag::kDesignatorMaximum:
    case Item::Tag::kStringIndex:
    case Item::Tag::kStringMinimum:
    case Item::Tag::kStringMaximum:
    case Item::Tag::kDelimiter:
        return kParseUnsuported;
    default: return kParseInvalidTag;
    }
}

ParseResult ProcessItem(const hid::Item& item, ParseState* state) {
    switch (item.type()) {
    case Item::Type::kMain: return ProcessMainItem(item, state);
    case Item::Type::kGlobal: return ProcessGlobalItem(item, state);
    case Item::Type::kLocal: return ProcessLocalItem(item, state);
    default: return kParseInvalidItemType;
    }
}

}  // namespace impl

ParseResult ParseReportDescriptor(
    const uint8_t* rpt_desc, size_t desc_len,
    DeviceDescriptor** device) {

    impl::ParseState state;

    if (!state.Init())
        return kParseNoMemory;

    const uint8_t* buf = rpt_desc;
    size_t len = desc_len;
    while (len > 0) {
        size_t actual = 0;
        auto item = hid::Item::ReadNext(buf, len, &actual);
        if (actual > len)
            return kParseMoreNeeded;
        if (actual == 0)
            return kParseUnsuported;

        auto pr = ProcessItem(item, &state);
        if (pr != kParseOk)
            return pr;

        len -= actual;
        buf += actual;
    }

    return kParseOk;
}

}  // namespace hid
