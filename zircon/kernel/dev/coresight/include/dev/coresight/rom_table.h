// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_CORESIGHT_INCLUDE_DEV_CORESIGHT_ROM_TABLE_H_
#define ZIRCON_KERNEL_DEV_CORESIGHT_INCLUDE_DEV_CORESIGHT_ROM_TABLE_H_

#include <lib/fitx/result.h>
#include <zircon/assert.h>

#include <string_view>
#include <utility>

#include <dev/coresight/component.h>
#include <hwreg/bitfields.h>

namespace coresight {

// [CS] D5
// A ROM table is a basic component (of type
// ComponentIDRegister::Class::k0x1ROMTable or
// ComponentIDRegister::Class::kCoreSight) that provides pointers to other
// components (including other ROM tables) in its lower registers. It is an
// organizational structure that can be used to find all CoreSight components
// on a chip (or system of chips). Thought of as a tree, the leaves are the
// system's CoreSight components and the root is typically referred to as the
// "base ROM table" (or, more plainly, "the ROM table").
class ROMTable {
 public:
  // [CS] D6.4.4
  struct Class0x1Entry : public hwreg::RegisterBase<Class0x1Entry, uint32_t> {
    DEF_FIELD(31, 12, offset);
    DEF_RSVDZ_FIELD(11, 9);
    DEF_FIELD(8, 4, powerid);
    DEF_RSVDZ_BIT(3);
    DEF_BIT(2, powerid_valid);
    DEF_BIT(1, format);
    DEF_BIT(0, present);

    static auto GetAt(uint32_t offset, uint32_t N) {
      return hwreg::RegisterAddr<Class0x1Entry>(offset +
                                                N * static_cast<uint32_t>(sizeof(uint32_t)));
    }
  };

  // [CS] D7.5.17
  struct Class0x9NarrowEntry : public hwreg::RegisterBase<Class0x9NarrowEntry, uint32_t> {
    DEF_FIELD(31, 12, offset);
    DEF_RSVDZ_FIELD(11, 9);
    DEF_FIELD(8, 4, powerid);
    DEF_RSVDZ_BIT(3);
    DEF_BIT(2, powerid_valid);
    DEF_FIELD(1, 0, present);

    static auto GetAt(uint32_t offset, uint32_t N) {
      return hwreg::RegisterAddr<Class0x9NarrowEntry>(offset +
                                                      N * static_cast<uint32_t>(sizeof(uint32_t)));
    }
  };

  // [CS] D7.5.17
  struct Class0x9WideEntry : public hwreg::RegisterBase<Class0x9WideEntry, uint64_t> {
    DEF_FIELD(63, 12, offset);
    DEF_RSVDZ_FIELD(11, 9);
    DEF_FIELD(8, 4, powerid);
    DEF_RSVDZ_BIT(3);
    DEF_BIT(2, powerid_valid);
    DEF_FIELD(1, 0, present);

    static auto GetAt(uint32_t offset, uint32_t N) {
      return hwreg::RegisterAddr<Class0x9WideEntry>(offset +
                                                    N * static_cast<uint32_t>(sizeof(uint64_t)));
    }
  };

  // [CS] D7.5.10
  struct Class0x9DeviceIDRegister : public hwreg::RegisterBase<Class0x9DeviceIDRegister, uint32_t> {
    DEF_RSVDZ_FIELD(31, 6);
    DEF_BIT(5, prr);
    DEF_BIT(4, sysmem);
    DEF_FIELD(3, 0, format);

    static auto GetAt(uint32_t offset) {
      return hwreg::RegisterAddr<Class0x9DeviceIDRegister>(offset + 0xfcc);
    }
  };

  // Conceptually, we construct a ROM table from a span of bytes, the span
  // being required to contain all of the components that the table
  // transitively refers to.
  ROMTable(uintptr_t base, uint32_t span_size) : base_(base), span_size_(span_size) {}

  // The underlying tree of tables is walked with no dynamic allocation,
  // calling a ComponentCallback on each CoreSight component found, the
  // callback having a signature of uintptr_t -> void.
  //
  // The callback is also called on any global timestamp generator components
  // found; these morally should be CoreSight components and ARM includes them
  // as top-level table entries. A timestamp generator can be characterized as
  // having a class of |ComponentIDRegister::Class::kNonStandard| and a part ID
  // of |arm::partid::kTimestampGenerator|.
  template <typename IoProvider, typename ComponentCallback>
  fitx::result<std::string_view> Walk(IoProvider io, ComponentCallback&& callback) {
    return WalkFrom(io, callback, 0);
  }

 private:
  // [CS] D6.2.1, D7.2.1
  // The largest possible ROM table entry index, for various types.
  static constexpr uint32_t k0x1EntryUpperBound = 960u;
  static constexpr uint32_t k0x9NarrowEntryUpperBound = 512u;
  static constexpr uint32_t k0x9WideEntryUpperBound = 256u;

  // [CS] D7.5.10
  static constexpr uint8_t kDevidFormatNarrow = 0x0;
  static constexpr uint8_t kDevidFormatWide = 0x1;

  // There are several types of ROM table entry registers; this struct serves
  // as unified front-end for accessing their contents.
  struct EntryContents {
    uint64_t value;
    uint32_t offset;
    bool present;
  };

  template <typename IoProvider, typename ComponentCallback>
  fitx::result<std::string_view> WalkFrom(IoProvider io, ComponentCallback&& callback,
                                          uint32_t offset) {
    const ComponentIDRegister::Class classid =
        ComponentIDRegister::GetAt(offset).ReadFrom(&io).classid();
    const DeviceArchRegister arch_reg = DeviceArchRegister::GetAt(offset).ReadFrom(&io);
    const auto architect = static_cast<uint16_t>(arch_reg.architect());
    const auto archid = static_cast<uint16_t>(arch_reg.archid());
    if (IsTable(classid, architect, archid)) {
      const auto format =
          static_cast<uint8_t>(Class0x9DeviceIDRegister::GetAt(offset).ReadFrom(&io).format());

      fitx::result<std::string_view, uint32_t> upper_bound = EntryIndexUpperBound(classid, format);
      if (upper_bound.is_error()) {
        return fitx::error(upper_bound.error_value());
      }

      for (uint32_t i = 0; i < upper_bound.value(); ++i) {
        fitx::result<std::string_view, EntryContents> read_entry_result =
            ReadEntryAt(io, offset, i, classid, format);
        if (read_entry_result.is_error()) {
          return fitx::error(read_entry_result.error_value());
        }
        EntryContents contents = read_entry_result.value();
        if (contents.value == 0) {
          break;  // Terminal entry if identically zero.
        } else if (!contents.present) {
          continue;
        }
        // [CS] D5.4
        // the offset provided by the ROM table entry requires a shift of 12 bits.
        uint32_t new_offset = offset + (contents.offset << 12);
        if (new_offset + kMinimumComponentSize > span_size_) {
          printf("does not fit: (view size, offset) = (%u, %u)\n", span_size_, new_offset);
          return fitx::error("does not fit");
        }
        if (fitx::result<std::string_view> walk_result = WalkFrom(io, callback, new_offset);
            walk_result.is_error()) {
          return walk_result;
        }
      }
      return fitx::ok();
    }

    const uint16_t partid = GetPartIDAt(io, offset);
    if (IsTerminalEntry(offset, classid, partid)) {
      std::forward<ComponentCallback>(callback)(base_ + offset);
      return fitx::ok();
    }

    printf(
        "expected ROM table or component at offset %u: "
        "(class, architect, archid) = (%#x (%s), %#x, %#x)",
        offset, static_cast<uint8_t>(classid), ToString(classid).data(), architect, archid);
    return fitx::error("unexpected component found");
  }

  bool IsTable(ComponentIDRegister::Class classid, uint16_t architect, uint16_t archid) const;

  bool IsTerminalEntry(uint32_t offset, ComponentIDRegister::Class classid, uint16_t partid);

  fitx::result<std::string_view, uint32_t> EntryIndexUpperBound(ComponentIDRegister::Class classid,
                                                                uint8_t format) const;

  template <typename IoProvider>
  fitx::result<std::string_view, EntryContents> ReadEntryAt(IoProvider io, uint32_t offset,
                                                            uint32_t N,
                                                            ComponentIDRegister::Class classid,
                                                            uint8_t format) {
    if (classid == ComponentIDRegister::Class::k0x1ROMTable) {
      auto entry = Class0x1Entry::GetAt(offset, N).ReadFrom(&io);
      return fitx::ok(EntryContents{
          .value = entry.reg_value(),
          .offset = static_cast<uint32_t>(entry.offset()),
          .present = static_cast<bool>(entry.present()),
      });
    } else if (classid == ComponentIDRegister::Class::kCoreSight) {
      // [CS] D7.5.17: only a value of 0b11 for present() signifies presence.
      switch (format) {
        case kDevidFormatNarrow: {
          auto entry = Class0x9NarrowEntry::GetAt(offset, N).ReadFrom(&io);
          return fitx::ok(EntryContents{
              .value = entry.reg_value(),
              .offset = static_cast<uint32_t>(entry.offset()),
              .present = static_cast<bool>(entry.present() & 0b11),
          });
        }
        case kDevidFormatWide: {
          auto entry = Class0x9WideEntry::GetAt(offset, N).ReadFrom(&io);
          uint64_t u32_offset = entry.offset() & 0xffffffff;
          if (entry.offset() != u32_offset) {
            return fitx::error(
                "a simplifying assumption is made that a ROM table entry's offset only contains 32 "
                "bits of information. If this is no longer true, please file a bug.");
          }
          return fitx::ok(EntryContents{
              .value = entry.reg_value(),
              .offset = static_cast<uint32_t>(u32_offset),
              .present = static_cast<bool>(entry.present() & 0b11),
          });
        }
        default:
          printf("bad format value: %#x", format);
          return fitx::error("bad format value");
      }
    }
    return fitx::error("not a ROM table");
  }

  uintptr_t base_;
  uint32_t span_size_;
};

}  // namespace coresight

#endif  // ZIRCON_KERNEL_DEV_CORESIGHT_INCLUDE_DEV_CORESIGHT_ROM_TABLE_H_
