// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_RELOCATION_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_RELOCATION_H_

#include <lib/stdcompat/bit.h>
#include <lib/stdcompat/span.h>

#include <algorithm>
#include <type_traits>
#include <variant>

#include "machine.h"

namespace elfldltl {

// This represents the ELF metadata in an ELF file that directs what dynamic
// relocation it requires.  It holds spans of the various raw relocation record
// types and provides a uniform visitor pattern for iterating over them.
//
// Both REL and RELA formats are tracked here.  Within each format, "relative"
// (simple fixup) relocation and "symbolic" (general) relocations are provided
// as separate subspans.  The RELR format is held as a raw span of words.  PLT
// (JMPREL) relocations use either REL or RELA format (but can't have both in
// parallel as general relocations do) and so are represented using a
// std::variant type across the REL and RELA container formats.
//
// The VisitRelative and VisitSymbolic methods can be used like std::visit to
// call a callback function (a generic lambda or other polymorphic callable)
// with each record, stopping early if the callback returns false.
// VisitSymbolic passes either an Elf::Rela or an Elf::Rel that uses an
// in-place addend.  VisitRelative passes either an Elf::Rela with separate
// address and addend, or an Elf::size_type address using an in-place addend.

template <class Elf>
class RelocationInfo {
 public:
  using size_type = typename Elf::size_type;
  using Addr = typename Elf::Addr;
  using Rel = typename Elf::Rel;
  using Rela = typename Elf::Rela;

  // These span types hold the various relocation tables in their raw forms.
  // The JMPREL table is in either REL or RELA format, so a variant is used.
  using RelTable = cpp20::span<const Rel>;
  using RelaTable = cpp20::span<const Rela>;
  using RelrTable = cpp20::span<const Addr>;
  using JmprelTable = std::variant<RelTable, RelaTable>;

  // Fetch the various relocation tables.  The REL and RELA tables have
  // relative and symbolic subsets.  The RELR table needs further decoding.
  // Enumeration should use the VisitRelative and VisitSymbolic methods, below.

  constexpr RelTable rel_relative() const { return rel_.subspan(0, relcount_); }

  constexpr RelTable rel_symbolic() const { return rel_.subspan(relcount_); }

  constexpr RelaTable rela_relative() const { return rela_.subspan(0, relacount_); }

  constexpr RelaTable rela_symbolic() const { return rela_.subspan(relacount_); }

  constexpr RelrTable relr() const { return relr_; }

  constexpr JmprelTable jmprel() const { return jmprel_; }

  // Install data for the various relocation tables.  These return *this so
  // they can be called in fluent style, e.g. in a constexpr initializer.

  constexpr RelocationInfo& set_rel(RelTable relocs, size_type relcount) {
    rel_ = relocs;
    relcount_ = relcount;
    return *this;
  }

  constexpr RelocationInfo& set_rela(RelaTable relocs, size_type relacount) {
    rela_ = relocs;
    relacount_ = relacount;
    return *this;
  }

  constexpr RelocationInfo& set_relr(RelrTable table) {
    relr_ = table;
    return *this;
  }

  constexpr RelocationInfo& set_jmprel(JmprelTable table) {
    jmprel_ = table;
    return *this;
  }

  // Return the number of valid entries in the table, from rel_relative(),
  // rela_relative(), or relr().  Hence returns relocs.size() if all entries
  // are valid, or else the index of the first invalid entry.

  template <ElfMachine Machine, class Reloc>  // DT_REL or DT_RELA
  static size_t ValidateRelative(cpp20::span<const Reloc> relocs) {
    constexpr auto valid = [](const auto& reloc) -> bool {
      constexpr uint32_t relative_type =
          static_cast<uint32_t>(RelocationTraits<Machine>::Type::kRelative);
      return reloc.type() == relative_type;
    };
    return std::count_if(relocs.begin(), relocs.end(), valid);
  }

  static size_t ValidateRelative(cpp20::span<const Addr> relocs) {  // DT_RELR
    // The first entry must be a fresh address (low bit clear), and all
    // possible bit patterns are valid for all subsequent entries.
    return (relocs.empty() || (relocs.front() & 1) != 0) ? 0 : relocs.size();
  }

  // Call visit(Elf::Rela reloc) -> bool or visit(Elf::size_type addr) on every
  // location needing simple fixup.  The Elf::size_type signature indicates the
  // addend is to be read from the relocated address itself.  Returns false the
  // first time visit returns false, otherwise true.
  template <typename Visitor>
  constexpr bool VisitRelative(Visitor&& visit) const {
    static_assert(std::is_invocable_r_v<bool, Visitor, size_type>);

    auto visit_all_rel = VisitAll([&visit](const Rel& reloc) -> bool {
      size_type location = reloc.offset;
      return visit(location);
    });

    auto visit_all_rela = VisitAll(visit);

    auto visit_all_relr = [&visit](RelrTable relr) -> bool {
      size_type r_offset = 0;  // Implied r_offset value for the last entry.
      for (size_type entry : relr) {
        // If the low bit is clear, this is a new address.
        // This is like an Elf::Rel record with offset = entry.
        if ((entry & 1) == 0) {
          r_offset = entry;
          if (!visit(r_offset)) {
            return false;
          }
        } else {
          // This is a bitmap representing the next Elf::kAddressBits - 1
          // address-size words after r_offset.
          size_type bitmap = entry >> 1;

          // The low bit corresponds to the word after the r_offset value left
          // from the last entry, so advance r_offset a word for every bit.
          size_type bitmap_offset = r_offset;
          r_offset += (Elf::kAddressBits - 1) * sizeof(size_type);

          // Now visit the address corresponding to each one bit, as if there
          // were an Elf::Rel record with the r_offset implied by this bit
          // position incrementing the running address by address-size per bit.
          while (bitmap != 0) {
            int skip = cpp20::countr_zero(bitmap) + 1;
            bitmap >>= skip;
            bitmap_offset += skip * sizeof(size_type);
            if (!visit(bitmap_offset)) {
              return false;
            }
          }
        }
      }
      return true;
    };

    return visit_all_rel(rel_relative()) && visit_all_rela(rela_relative()) &&
           visit_all_relr(relr());
  }

  // Call visit(Elf::Rel) -> bool or visit(Elf::Rela) -> bool on every symbolic
  // relocation record.  Returns false the first time visit returns false,
  // otherwise true.
  template <typename Visitor>
  constexpr bool VisitSymbolic(Visitor&& visit) const {
    static_assert(std::is_invocable_r_v<bool, Visitor, const Rel&>);
    static_assert(std::is_invocable_r_v<bool, Visitor, const Rela&>);

    auto visit_all = VisitAll(std::forward<Visitor>(visit));

    return visit_all(rel_symbolic()) && visit_all(rela_symbolic()) &&
           std::visit(visit_all, jmprel_);
  }

 private:
  // Returns lambda visit_all(span<T>) -> bool that calls visit(T) -> bool.
  static constexpr auto VisitAll = [](auto&& visit) {
    return [visit = std::forward<decltype(visit)>(visit)](auto table) -> bool {
      return std::all_of(table.begin(), table.end(), visit);
    };
  };

  RelTable rel_;
  size_type relcount_ = 0;
  RelaTable rela_;
  size_type relacount_ = 0;
  RelrTable relr_;
  JmprelTable jmprel_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_RELOCATION_H_
