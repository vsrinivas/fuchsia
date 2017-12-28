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
        uint32_t twobit = (bitfield & 0x1) ? 0x02 : 0x01;
        result |= twobit << (2 * ix);
        bitfield >>= 1;
    }
    return result;
}

// Helper template class that translates pointers from two different
// array allocations using memory offsets.
template <typename T>
class Fixup {
public:
    Fixup(const T* src_base, T* dest_base)
        : src_base_(src_base), dest_base_(dest_base) {}

    T* operator()(const T* src) {
        return (src == nullptr) ? nullptr : dest_base_ + (src - src_base_);
    }

private:
    const T* const src_base_;
    T* const dest_base_;
};

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
          parent_coll_(nullptr),
          report_id_count_(0) {
    }

    bool Init() {
        fbl::AllocChecker ac;
        coll_.reserve(kMaxCollectionCount, &ac);
        return ac.check();
    }

    ParseResult Finish(DeviceDescriptor** device) {
        // A single heap allocation is used to pack the constructed model so
        // that the client can do a single free. There are 3 arrays in
        // order:
        //  - The report array, one entry per unique report id.
        //  - all reports fields
        //  - all collections
        //
        // The bottom two need fixups. Which means that inner pointers that
        // refer to collections in the source need to be translated to pointers
        // valid within the destination memory area.

        if (report_id_count_ == 0) {
            // The reports don't have an id. This is scenario #1 as
            // explained in the header.
            report_id_count_ = 1;
        }

        size_t device_sz =
            sizeof(DeviceDescriptor) + report_id_count_ * sizeof(ReportDescriptor);
        size_t fields_sz = fields_.size() * sizeof(ReportField);
        size_t collect_sz = coll_.size() * sizeof(Collection);

        fbl::AllocChecker ac;
        auto mem = new (&ac) char[device_sz + fields_sz + collect_sz];
        if (!ac.check())
            return kParseNoMemory;

        auto dev = new (mem) DeviceDescriptor { report_id_count_, {} };

        mem += device_sz;
        auto dest_fields = reinterpret_cast<ReportField*>(mem);

        mem += fields_sz;
        auto dest_colls = reinterpret_cast<Collection*>(mem);

        Fixup<Collection> coll_fixup(&coll_[0], &dest_colls[0]);

        size_t ix = 0u;
        // Copy and fix the collections first.
        for (const auto& c: coll_) {
            dest_colls[ix] = c;
            dest_colls[ix].parent = coll_fixup(c.parent);
            ++ix;
        }

        // Copy and fix the fields next.
        ix = 0u;
        size_t ifr = 0u;
        int32_t last_id = -1;
        size_t count = 0;

        for (const auto& f: fields_) {
            dest_fields[ix] = f;
            dest_fields[ix].col = coll_fixup(f.col);

            if (static_cast<int32_t>(f.report_id) != last_id) {
                // New report id. Fill the next ReportDescriptor entry with the address
                // of the first field, the new report id.
                dev->report[ifr] = ReportDescriptor { f.report_id, 0, &dest_fields[ix] };

                if (ifr != 0) {
                    // Update the previous ReportDescriptor with the field count.
                    dev->report[ifr - 1].count = count;
                }

                last_id = f.report_id;
                count = 0;
                ++ifr;
            }

            ++count;
            ++ix;
        }

        if (ifr != 0) {
            // Last ReportDescriptor need field count updated.
            dev->report[ifr - 1].count = count;
        }

        *device = dev;
        return kParseOk;
    }

    ParseResult start_collection(uint32_t data) {                 // Main
        if (!is_valid_collection(data))
            return kParseInvalidItemValue;

        // By reserving and doing the size() check here, we ensure
        // never a re-alloc, keeping the intra-collection pointers valid.
        if (coll_.size() > kMaxCollectionCount)
            return kParseOverflow;

        if (parent_coll_ == nullptr) {
            // The first collection must be an application collection.
            if (!is_app_collection(data))
                return kParseUnexpectedCol;
        }

        uint16_t usage = usages_.is_empty() ? 0 : usages_[0];

        Collection col {
            static_cast<CollectionType>(data),
            Usage {table_.attributes.usage.page, usage},
            parent_coll_
        };

        coll_.push_back(col);
        parent_coll_ = &coll_[coll_.size() - 1];
        return kParseOk;
    }

    ParseResult end_collection(uint32_t data) {
        if (data != 0u)
            return kParseInvalidItemValue;
        if (parent_coll_ == nullptr)
            return kParseInvalidTag;
        // We don't free collection items until Finish().
        parent_coll_ = parent_coll_->parent;
        // TODO(cpu): make sure there are fields between start and end
        // otherwise this is malformed.
        return kParseOk;
    }

    ParseResult add_field(NodeType type, uint32_t data) {       // Main
        if (coll_.size() == 0)
            return kParseUnexectedItem;

        if (!validate_ranges())
            return kParseInvalidRange;

        auto flags = expand_bitfield(data);
        Attributes attributes = table_.attributes;

        UsageIterator usage_it(this, flags);

        for (uint32_t ix = 0; ix != table_.report_count; ++ ix) {
            if (!usage_it.next_usage(&attributes.usage.usage))
                return kParseInvalidUsage;

            auto curr_col = &coll_[coll_.size() - 1];

            ReportField field {
                table_.report_id,
                attributes,
                type,
                flags,
                curr_col
            };

            fbl::AllocChecker ac;
            fields_.push_back(field, &ac);
            if (!ac.check())
                return kParseNoMemory;
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

    ParseResult set_logical_min(int32_t data) {                // Global
        table_.attributes.logc_mm.min = data;
        return kParseOk;
    }

    ParseResult set_logical_max(int32_t data) {                // Global
        table_.attributes.logc_mm.max = data;
        return kParseOk;
    }

    ParseResult set_physical_min(int32_t data) {                // Global
        table_.attributes.phys_mm.min = data;
        return kParseOk;
    }

    ParseResult set_physical_max(int32_t data) {                // Global
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
        ++report_id_count_;
        return kParseOk;
    }

    ParseResult set_report_count(uint32_t data) {               // Global
        table_.report_count = data;
        return kParseOk;
    }

    ParseResult set_report_size(uint32_t data) {                // Global
        if (data > UINT8_MAX)
            return kParseInvalidRange;
        table_.attributes.bit_sz = static_cast<uint8_t>(data);
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
    };

    // Helper class that encapsulates the logic of assigning usages
    // to input, output and feature items. There are 2 ways to
    // assign usages:
    // 1- from the UsageMinimum and UsageMaximum items
    // 2- from the existing Usage items stored in the usages_ vector
    //
    // For method #2, the number of calls to next_usage() can be
    // greater than the available usages. The standard requires in
    // this case to return the last usage in all remaining calls.
    //
    // If the item is an array, only returns the first usage.
    // TODO(cpu): this is not completely right, at least reading
    // from the example in the spec pages 76-77, we are presented
    // the 17-key keypad:
    //      Usage(0), Usage Minimum(53h), Usage Maximum(63h),
    //      Logical Minimum (0), Logical Maximum (17),
    //      Report Size (8), Report Count (3)
    //      Input (Data, Array)
    //  But on the other hand, the adafruit trinket presents:
    //      Report Count (1), Report Size (2)
    //      Usage (Sys Sleep (82)), Usage (Sys Power Down(81)), Usage (Sys Wake Up(83))
    //      Input (Data,Array)
    //
    // In other words, array might need a lookup table to be generated
    // see the header for a potential approach.

    class UsageIterator {
    public:
        UsageIterator(ParseState* ps, uint32_t field_type)
            : usages_(nullptr),
              usage_range_(),
              index_(0),
              last_usage_(0),
              is_array_(FieldTypeFlags::kArray & field_type) {
            if ((ps->usage_range_.max == 0) && (ps->usage_range_.min == 0)) {
                usages_ = &ps->usages_;
            } else {
                usage_range_ = ps->usage_range_;
            }
        }

        bool next_usage(uint16_t* usage) {
            if (usages_ == nullptr) {
                *usage = static_cast<uint16_t>(usage_range_.min + index_);
                if (*usage > usage_range_.max)
                    return false;
            } else {
                *usage = (index_ < usages_->size()) ? (*usages_)[index_] : last_usage_;
                last_usage_ = *usage;
            }
            if (!is_array_)
                ++index_;
            return true;
        }

    private:
        const fbl::Vector<uint16_t>* usages_;
        MinMax usage_range_;
        uint32_t index_;
        uint16_t last_usage_;
        bool is_array_;
    };

    bool validate_ranges() const {
        if (usage_range_.min > usage_range_.max)
            return false;
        if (table_.attributes.logc_mm.min > table_.attributes.logc_mm.max)
            return false;
        return true;
    }

    // Internal state per spec:
    MinMax usage_range_;
    StateTable table_;
    fbl::Vector<StateTable> stack_;
    fbl::Vector<uint16_t> usages_;
    // Temporary output model:
    Collection* parent_coll_;
    size_t report_id_count_;
    fbl::Vector<Collection> coll_;
    fbl::Vector<ReportField> fields_;
};

ParseResult ProcessMainItem(const hid::Item& item, ParseState* state) {
    ParseResult res;
    switch (item.tag()) {
    case Item::Tag::kInput:
    case Item::Tag::kOutput:
    case Item::Tag::kFeature:
        res = state->add_field(static_cast<NodeType>(item.tag()), item.data());
        break;
    case Item::Tag::kCollection: res = state->start_collection(item.data());
        break;
    case Item::Tag::kEndCollection: res = state->end_collection(item.data());
        break;
    default: res = kParseInvalidTag;
    }

    return (res == kParseOk)? state->reset_usage() : res;
}

ParseResult ProcessGlobalItem(const hid::Item& item, ParseState* state) {
    switch (item.tag()) {
    case Item::Tag::kUsagePage: return state->set_usage_page(item.data());
    case Item::Tag::kLogicalMinimum: return state->set_logical_min(item.signed_data());
    case Item::Tag::kLogicalMaximum: return state->set_logical_max(item.signed_data());
    case Item::Tag::kPhysicalMinimum: return state->set_physical_min(item.signed_data());
    case Item::Tag::kPhysicalMaximum: return state->set_physical_max(item.signed_data());
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

    return state.Finish(device);
}

}  // namespace hid
