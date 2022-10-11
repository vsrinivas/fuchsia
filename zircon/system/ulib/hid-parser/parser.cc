// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/vector.h>
#include <hid-parser/item.h>
#include <hid-parser/parser.h>
#include <safemath/safe_math.h>

using safemath::CheckAdd, safemath::CheckMul;

namespace {

// The maximum size in bytes for all the HID descriptor data. While in theory
// we can support much more, our memory use checkers are not happy with more.
// See http://fxbug.dev/64791.
constexpr size_t kMaxTotalDescriptorByteCount = 1 << 30;  // 1GB

// The maximum number of reports that we support for a single device. This
// prevents malformed descriptors from using up useful memory, and should be
// generous enough to handle most devices.
// See http://fxbug.dev/106491
constexpr size_t kMaxReports = 10000;

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

// Temporary structure to keep report id data.
struct ReportID {
  bool has_report_id;
  uint8_t report_id;
  size_t input_count;
  size_t output_count;
  size_t feature_count;
};

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
  Fixup(const T* src_base, T* dest_base) : src_base_(src_base), dest_base_(dest_base) {}

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

bool report_id_has_items(ReportID id) {
  return (id.input_count != 0) || (id.feature_count != 0) || (id.output_count != 0);
}

class ParseState {
 public:
  static char* Alloc(size_t size) {
    fbl::AllocChecker ac;
    auto mem = new (&ac) char[size];
    return ac.check() ? mem : nullptr;
  }

  static void Free(void* mem) { delete[] reinterpret_cast<char*>(mem); }

  ParseState() : usage_range_(), table_(), parent_coll_(nullptr) {
    // First 8 bits of a report are the report ID.
    table_.attributes.offset = 8;
  }

  bool Init() {
    fbl::AllocChecker ac;

    // Add the initial report ID representing "no report_id".
    ReportID empty_report_id = {false, 0, 0, 0, 0};
    report_ids_.push_back(empty_report_id, &ac);
    if (!ac.check())
      return false;
    table_.report_ids_index = 0;

    return true;
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

    // Check if we actually have items or report ids before going forward.
    if (coll_size_ == 0) {
      return kParseMoreNeeded;
    }

    // If there's one report in |report_ids_| then there's just the null id.
    bool no_report_id = report_ids_.size() == 1;

    // If we have ids, then erase the null id since it shouldn't have items in it.
    if (!no_report_id) {
      report_ids_.erase(0);
    }
    size_t report_count = report_ids_.size();

    // Too many reports.
    if (report_count >= kMaxReports) {
      // Sorry, not sorry.
      return kParseNoMemory;
    }

    size_t device_sz;
    if (!CheckAdd(sizeof(DeviceDescriptor), CheckMul(report_count, sizeof(ReportDescriptor)))
             .AssignIfValid(&device_sz)) {
      return kParseNoMemory;
    }
    size_t fields_sz;
    if (!CheckMul(fields_.size(), sizeof(ReportField)).AssignIfValid(&fields_sz)) {
      return kParseNoMemory;
    }
    size_t collect_sz;
    if (!CheckMul(coll_size_, sizeof(Collection)).AssignIfValid(&collect_sz)) {
      return kParseNoMemory;
    }

    size_t total_sz;
    if (!CheckAdd(device_sz, CheckAdd(fields_sz, collect_sz)).AssignIfValid(&total_sz) ||
        total_sz > kMaxTotalDescriptorByteCount) {
      return kParseNoMemory;
    }
    auto mem = Alloc(total_sz);
    if (mem == nullptr)
      return kParseNoMemory;

    auto dev = new (mem) DeviceDescriptor{report_count};

    mem += device_sz;
    auto dest_fields = reinterpret_cast<ReportField*>(mem);

    mem += fields_sz;
    auto dest_colls = reinterpret_cast<Collection*>(mem);

    Fixup<Collection> coll_fixup(coll_.data(), dest_colls);

    // Copy and fix the collections first.
    for (size_t i = 0; i < coll_size_; i++) {
      dest_colls[i] = coll_[i];
      dest_colls[i].parent = coll_fixup(coll_[i].parent);
    }

    // Copy and fix the fields next.
    size_t ix = 0u;
    size_t ifr = 0u;

    for (const ReportID& report_id : report_ids_) {
      // If we don't have report ids, we start at 0 offset.
      // Otherwise we start at an 8 bit offset because the first
      // byte is the report id.
      size_t input_bit_sz = (no_report_id) ? 0 : 8;
      size_t input_count = 0;
      ReportField* input_fields_start = &dest_fields[ix];

      size_t output_bit_sz = (no_report_id) ? 0 : 8;
      size_t output_count = 0;
      ReportField* output_fields_start = &input_fields_start[report_id.input_count];

      size_t feature_bit_sz = (no_report_id) ? 0 : 8;
      size_t feature_count = 0;
      ReportField* feature_fields_start = &output_fields_start[report_id.output_count];

      // Fill the ReportDescriptor entry with the address
      // of the first field, and the report id.
      dev->report[ifr] = {};
      dev->report[ifr].report_id = report_id.report_id;

      dev->report[ifr].input_fields = input_fields_start;
      dev->report[ifr].output_fields = output_fields_start;
      dev->report[ifr].feature_fields = feature_fields_start;

      for (const auto& f : fields_) {
        if (f.report_id != report_id.report_id)
          continue;

        switch (f.type) {
          case NodeType::kInput:
            if (input_count == report_id.input_count)
              return kParseOverflow;
            input_fields_start[input_count] = f;
            input_fields_start[input_count].col = coll_fixup(f.col);
            input_fields_start[input_count].attr.offset = static_cast<uint32_t>(input_bit_sz);
            input_bit_sz += f.attr.bit_sz;
            ++input_count;
            break;
          case NodeType::kOutput:
            if (output_count == report_id.output_count)
              return kParseOverflow;
            output_fields_start[output_count] = f;
            output_fields_start[output_count].col = coll_fixup(f.col);
            output_fields_start[output_count].attr.offset = static_cast<uint32_t>(output_bit_sz);
            output_bit_sz += f.attr.bit_sz;
            ++output_count;
            break;
          case NodeType::kFeature:
            if (feature_count == report_id.feature_count)
              return kParseOverflow;
            feature_fields_start[feature_count] = f;
            feature_fields_start[feature_count].col = coll_fixup(f.col);
            feature_fields_start[feature_count].attr.offset = static_cast<uint32_t>(feature_bit_sz);
            feature_bit_sz += f.attr.bit_sz;
            ++feature_count;
            break;
        }

        ++ix;
      }

      // If we haven't seen any fields reset the size to 0.
      if (input_count == 0)
        input_bit_sz = 0;
      if (output_count == 0)
        output_bit_sz = 0;
      if (feature_count == 0)
        feature_bit_sz = 0;

      // Update the report ReportDescriptor with the final information.
      dev->report[ifr].input_count = input_count;
      dev->report[ifr].input_byte_sz = DIV_ROUND_UP(input_bit_sz, 8);

      dev->report[ifr].output_count = output_count;
      dev->report[ifr].output_byte_sz = DIV_ROUND_UP(output_bit_sz, 8);

      dev->report[ifr].feature_count = feature_count;
      dev->report[ifr].feature_byte_sz = DIV_ROUND_UP(feature_bit_sz, 8);

      ++ifr;
    }

    *device = dev;
    return kParseOk;
  }

  ParseResult start_collection(uint32_t data) {  // Main
    if (!is_valid_collection(data))
      return kParseInvalidItemValue;

    if (coll_size_ >= kMaxCollectionCount)
      return kParseOverflow;

    if (parent_coll_ == nullptr) {
      // The first collection must be an application collection.
      if (!is_app_collection(data))
        return kParseUnexpectedCol;
    }

    uint32_t usage = usages_.is_empty() ? 0 : usages_[0];

    Collection col{static_cast<CollectionType>(data), Usage{table_.attributes.usage.page, usage},
                   parent_coll_};

    coll_[coll_size_++] = col;
    parent_coll_ = &coll_[coll_size_ - 1];
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

  ParseResult add_field(NodeType type, uint32_t data) {  // Main
    if (coll_size_ == 0)
      return kParseUnexpectedItem;

    if (!validate_ranges())
      return kParseInvalidRange;

    auto flags = expand_bitfield(data);
    Attributes attributes = table_.attributes;
    UsageIterator usage_it(this, flags);

    for (uint32_t ix = 0; ix != table_.report_count; ++ix) {
      attributes.usage = usage_it.next_usage();

      auto curr_col = &coll_[coll_size_ - 1];

      ReportField field{report_ids_[table_.report_ids_index].report_id, attributes, type, flags,
                        curr_col};

      // If physical min/max are zero they should be set to logical values.
      if (field.attr.phys_mm.min == 0 && field.attr.phys_mm.max == 0) {
        field.attr.phys_mm.min = field.attr.logc_mm.min;
        field.attr.phys_mm.max = field.attr.logc_mm.max;
      }

      fbl::AllocChecker ac;
      fields_.push_back(field, &ac);
      if (!ac.check())
        return kParseNoMemory;

      switch (type) {
        case kInput:
          report_ids_[table_.report_ids_index].input_count++;
          break;
        case kOutput:
          report_ids_[table_.report_ids_index].output_count++;
          break;
        case kFeature:
          report_ids_[table_.report_ids_index].feature_count++;
          break;
      }
    }

    return kParseOk;
  }

  ParseResult reset_usage() {  //  after each Main
    // Is it an error to drop pending usages?
    usages_.reset();
    usage_range_ = {};
    return kParseOk;
  }

  ParseResult add_usage(uint32_t data) {  // Local
    usages_.push_back(data);
    return kParseOk;
  }

  ParseResult set_usage_min(uint32_t data) {  // Local
    if (data > UINT16_MAX)
      return kParseUnsuported;

    usage_range_.min = data;
    return kParseOk;
  }

  ParseResult set_usage_max(uint32_t data) {  // Local
    // In add_usage() we don't restrict the value while
    // here we do. This is because a very large range can
    // effectively DoS the code in the usage iterator.
    if (data > UINT16_MAX)
      return kParseUnsuported;

    usage_range_.max = data;
    return kParseOk;
  }

  ParseResult set_usage_page(uint32_t data) {  // Global
    if (data > UINT16_MAX)
      return kParseInvalidRange;

    table_.attributes.usage.page = static_cast<uint16_t>(data);
    return kParseOk;
  }

  ParseResult set_logical_min(int32_t data) {  // Global
    table_.attributes.logc_mm.min = data;
    return kParseOk;
  }

  ParseResult set_logical_max(const hid::Item& item) {  // Global
    if (table_.attributes.logc_mm.min >= 0) {
      table_.attributes.logc_mm.max = item.data();
    } else {
      table_.attributes.logc_mm.max = item.signed_data();
    }
    return kParseOk;
  }

  ParseResult set_physical_min(int32_t data) {  // Global
    table_.attributes.phys_mm.min = data;
    return kParseOk;
  }

  ParseResult set_physical_max(const hid::Item& item) {  // Global
    if (table_.attributes.phys_mm.min >= 0) {
      table_.attributes.phys_mm.max = item.data();
    } else {
      table_.attributes.phys_mm.max = item.signed_data();
    }
    return kParseOk;
  }

  ParseResult set_unit(uint32_t data) {
    // The unit parsing is a complicated per nibble
    // accumulation of units. Leave that to application.
    table_.attributes.unit.type = data;
    return kParseOk;
  }

  ParseResult set_unit_exp(uint32_t data) {  // Global
    int32_t exp = static_cast<uint8_t>(data);
    // The exponent uses a weird, not fully specified
    // conversion, for example the value 0xf results
    // in -1 exponent. See USB HID spec doc.
    if (exp > 7)
      exp = exp - 16;
    table_.attributes.unit.exp = exp;
    return kParseOk;
  }

  ParseResult set_report_id(uint32_t data) {  // Global
    if (data == 0)
      return kParserInvalidID;
    if (data > UINT8_MAX)
      return kParseInvalidRange;

    // It's an error to define a report id after items have been defined with no id.
    if (report_id_has_items(report_ids_[0]))
      return kParserMissingID;

    // Check if we've seen the report id before.
    uint8_t id = static_cast<uint8_t>(data);
    ReportID* report_ids = report_ids_.data();
    for (size_t i = 0; i < report_ids_.size(); i++) {
      if (report_ids[i].report_id == id) {
        table_.report_ids_index = i;
        return kParseOk;
      }
    }

    // We haven't, so allocate the new report id.
    ReportID new_report_id = {true, id, 0, 0, 0};
    fbl::AllocChecker ac;
    report_ids_.push_back(new_report_id, &ac);
    if (!ac.check())
      return kParseNoMemory;
    table_.report_ids_index = report_ids_.size() - 1;

    return kParseOk;
  }

  ParseResult set_report_count(uint32_t data) {  // Global
    if (data > UINT16_MAX) {
      return kParseNoMemory;
    }
    table_.report_count = data;
    return kParseOk;
  }

  ParseResult set_report_size(uint32_t data) {  // Global
    if (data > UINT8_MAX)
      return kParseInvalidRange;
    table_.attributes.bit_sz = static_cast<uint8_t>(data);
    return kParseOk;
  }

  ParseResult push(uint32_t data) {  // Global
    fbl::AllocChecker ac;
    stack_.push_back(table_, &ac);
    return ac.check() ? kParseOk : kParseNoMemory;
  }

  ParseResult pop(uint32_t data) {  // Global
    if (stack_.size() == 0)
      return kParserUnexpectedPop;

    table_ = stack_[stack_.size() - 1];
    stack_.pop_back();
    return kParseOk;
  }

 private:
  struct StateTable {
    Attributes attributes;
    uint32_t report_count;
    // The index into the report_ids_ vector for the current ReportId object.
    size_t report_ids_index;
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
      usage_page_ = ps->table_.attributes.usage.page;
      if ((ps->usage_range_.max == 0) && (ps->usage_range_.min == 0)) {
        usages_ = &ps->usages_;
        has_usage_ = (usages_->size() != 0);
      } else {
        usage_range_ = ps->usage_range_;
        has_usage_ = true;
      }
    }

    Usage next_usage() {
      Usage usage = {};
      if (!has_usage_) {
        return usage;
      }

      int64_t usage_long = 0;
      if (usages_ == nullptr) {
        usage_long = usage_range_.min + index_;
        if (usage_long > usage_range_.max)
          usage_long = usage_range_.max;
      } else {
        usage_long = (index_ < usages_->size()) ? (*usages_)[index_] : last_usage_;
        last_usage_ = static_cast<uint32_t>(usage_long);
      }
      if (!is_array_) {
        ++index_;
      }

      usage.page = usage_page_;
      usage.usage = static_cast<uint32_t>(usage_long);

      return usage;
    }

   private:
    const fbl::Vector<uint32_t>* usages_;
    MinMax usage_range_;
    int16_t usage_page_;
    uint32_t index_;
    uint32_t last_usage_;
    bool is_array_;
    bool has_usage_;
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
  fbl::Vector<uint32_t> usages_;
  // Temporary output model:
  Collection* parent_coll_;
  fbl::Vector<ReportID> report_ids_;
  std::array<Collection, kMaxCollectionCount> coll_;
  size_t coll_size_ = 0;
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
    case Item::Tag::kCollection:
      res = state->start_collection(item.data());
      break;
    case Item::Tag::kEndCollection:
      res = state->end_collection(item.data());
      break;
    default:
      res = kParseInvalidTag;
  }

  return (res == kParseOk) ? state->reset_usage() : res;
}

ParseResult ProcessGlobalItem(const hid::Item& item, ParseState* state) {
  switch (item.tag()) {
    case Item::Tag::kUsagePage:
      return state->set_usage_page(item.data());
    case Item::Tag::kLogicalMinimum:
      return state->set_logical_min(item.signed_data());
    case Item::Tag::kLogicalMaximum:
      return state->set_logical_max(item);
    case Item::Tag::kPhysicalMinimum:
      return state->set_physical_min(item.signed_data());
    case Item::Tag::kPhysicalMaximum:
      return state->set_physical_max(item);
    case Item::Tag::kUnitExponent:
      return state->set_unit_exp(item.data());
    case Item::Tag::kUnit:
      return state->set_unit(item.data());
    case Item::Tag::kReportSize:
      return state->set_report_size(item.data());
    case Item::Tag::kReportId:
      return state->set_report_id(item.data());
    case Item::Tag::kReportCount:
      return state->set_report_count(item.data());
    case Item::Tag::kPush:
      return state->push(item.data());
    case Item::Tag::kPop:
      return state->pop(item.data());
    default:
      return kParseInvalidTag;
  }
}

ParseResult ProcessLocalItem(const hid::Item& item, ParseState* state) {
  switch (item.tag()) {
    case Item::Tag::kUsage:
      return state->add_usage(item.data());
    case Item::Tag::kUsageMinimum:
      return state->set_usage_min(item.data());
    case Item::Tag::kUsageMaximum:
      return state->set_usage_max(item.data());

    case Item::Tag::kDesignatorIndex:  // Fall thru. TODO(cpu) implement.
    case Item::Tag::kDesignatorMinimum:
    case Item::Tag::kDesignatorMaximum:
    case Item::Tag::kStringIndex:
    case Item::Tag::kStringMinimum:
    case Item::Tag::kStringMaximum:
    case Item::Tag::kDelimiter:
      return kParseUnsuported;
    default:
      return kParseInvalidTag;
  }
}

ParseResult ProcessItem(const hid::Item& item, ParseState* state) {
  switch (item.type()) {
    case Item::Type::kMain:
      return ProcessMainItem(item, state);
    case Item::Type::kGlobal:
      return ProcessGlobalItem(item, state);
    case Item::Type::kLocal:
      return ProcessLocalItem(item, state);
    default:
      return kParseInvalidItemType;
  }
}

}  // namespace impl

ParseResult ParseReportDescriptor(const uint8_t* rpt_desc, size_t desc_len,
                                  DeviceDescriptor** device) {
  impl::ParseState state;

  if (rpt_desc == nullptr || device == nullptr)
    return kParseMoreNeeded;

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

void FreeDeviceDescriptor(DeviceDescriptor* dev_desc) {
  // memory was allocated via ParseState::Alloc()
  impl::ParseState::Free(dev_desc);
}

Collection* GetAppCollection(const ReportField* field) {
  if (field == nullptr)
    return nullptr;

  auto collection = field->col;
  while (collection != nullptr) {
    if (collection->type == hid::CollectionType::kApplication)
      break;
    collection = collection->parent;
  }
  return collection;
}

#undef DIV_ROUND_UP
}  // namespace hid
