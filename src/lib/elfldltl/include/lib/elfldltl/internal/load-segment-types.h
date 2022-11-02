// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_LOAD_SEGMENT_TYPES_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_LOAD_SEGMENT_TYPES_H_

#include <cassert>
#include <string_view>

#include "../constants.h"
#include "../layout.h"
#include "../phdr.h"

namespace elfldltl::internal {

constexpr std::string_view kTooManyLoads = "too many PT_LOAD segments";

// This is used to implement the LoadInfo::ConstantSegment type below.
template <typename SegmentType>
class LoadConstantSegmentType : public SegmentType {
 public:
  using typename SegmentType::size_type;

  constexpr explicit LoadConstantSegmentType(size_type offset, size_type vaddr, size_type memsz,
                                             uint32_t flags)
      : SegmentType(offset, vaddr, memsz), flags_(flags) {}

  // The whole segment is loaded from the file.
  constexpr size_type filesz() const { return this->memsz(); }

  constexpr uint32_t flags() const { return flags_; }

  constexpr bool readable() const { return flags_ & Flags::kRead; }

  constexpr bool executable() const { return flags_ & Flags::kExecute; }

  constexpr bool relro() const { return flags_ & Flags::kWrite; }

 private:
  using Flags = PhdrBase::Flags;
  uint32_t flags_ = 0;
};

// This provides the several distinct LoadInfo::*Segment used below.
template <typename SizeType>
struct LoadSegmentTypes {
  using size_type = SizeType;

  // This is used for RELRO bounds.
  struct Region {
    size_type size() const { return end - start; }

    size_type empty() const { return start == end; }

    size_type start = 0, end = 0;
  };

  // Every kind of segment needs an offset and a size.
  class SegmentBase {
   public:
    using size_type = LoadSegmentTypes::size_type;

    constexpr size_type offset() const { return offset_; }

    constexpr size_type memsz() const { return memsz_; }

    constexpr explicit SegmentBase(size_type offset, size_type memsz)
        : offset_(offset), memsz_(memsz) {}

   protected:
    constexpr void set_offset(size_type offset) { offset_ = offset; }

    constexpr void set_memsz(size_type memsz) { memsz_ = memsz; }

   private:
    size_type offset_ = 0;
    size_type memsz_ = 0;
  };

  // Most generic segments need to record the vaddr separately from the offset.
  template <PhdrLoadPolicy Policy>
  class Segment : public SegmentBase {
   public:
    using typename SegmentBase::size_type;

    constexpr size_type vaddr() const { return vaddr_; }

    constexpr explicit Segment(size_type offset, size_type vaddr, size_type memsz)
        : SegmentBase(offset, memsz), vaddr_(vaddr) {}

   private:
    size_type vaddr_ = 0;
  };

  // TODO(mcgrathr): This is an optimization. GCC doesn't like the
  // specialization being here and won't allow it to be defined later either.
#ifdef __clang__
  // With constrained layout policy, the offset and vaddr don't both need to be
  // tracked.  They aren't always identical, but they always have a fixed
  // difference for the whole file.
  template <>
  class Segment<PhdrLoadPolicy::kContiguous> : public SegmentBase {
   public:
    using typename SegmentBase::size_type;

    constexpr size_type vaddr() const { return this->offset(); }

   protected:
    constexpr explicit Segment(size_type offset, size_type vaddr, size_type memsz)
        : SegmentBase(offset, memsz) {}
  };
#endif

  // A writable data segment (with no attached .bss) just identifies the pages
  // from the file to load.
  template <PhdrLoadPolicy Policy>
  struct DataSegment : public Segment<Policy> {
    using Base = Segment<Policy>;
    using Base::Base;

    constexpr explicit DataSegment(size_type offset, size_type vaddr, size_type memsz,
                                   size_type filesz)
        : Segment<Policy>(offset, vaddr, memsz) {
      assert(filesz == memsz);
    }

    // The whole segment is loaded from the file.
    constexpr size_type filesz() const { return this->memsz(); }
  };

  // A writable data segment with an attached zero-fill segment is both an
  // optimization and a way to share the partial page between the two without
  // extra page-alignment waste between the file portion and the zero portion.
  template <PhdrLoadPolicy Policy>
  class DataWithZeroFillSegment : public Segment<Policy> {
   public:
    constexpr explicit DataWithZeroFillSegment(size_type offset, size_type vaddr, size_type memsz,
                                               size_type filesz)
        : Segment<Policy>(offset, vaddr, memsz), filesz_(filesz) {}

    // Only a leading subset of the in-memory segment is loaded from the file.
    constexpr size_type filesz() const { return filesz_; }

   private:
    size_type filesz_ = 0;
  };

  // A constant segment tracks the readable() and executable() flags.
  template <PhdrLoadPolicy Policy>
  using ConstantSegment = LoadConstantSegmentType<Segment<Policy>>;

  // A plain zero-fill segment has nothing but anonymous pages to allocate.
  // The file offset is unused, so SegmentBase::offset() is actually the vaddr.
  class ZeroFillSegment : public SegmentBase {
   public:
    using SegmentBase::SegmentBase;

    constexpr size_type vaddr() const { return this->offset(); }

    constexpr size_type filesz() const { return 0; }
  };

  template <class First, class Second>
  static constexpr bool Adjacent(const First& first, const Second& second) {
    // In classes where vaddr() and offset() are the same, this might be doing
    // the same check twice but that will just get CSE.
    return first.vaddr() + first.memsz() == second.vaddr() &&
           first.offset() + first.memsz() == second.offset();
  }

  // ZeroFillSegment uses vaddr() for offset() so it might not match the
  // vaddr() of the preceding real data segment the generic version checks.
  template <class First>
  static constexpr bool Adjacent(const First& first, const ZeroFillSegment& second) {
    return first.vaddr() + first.memsz() == second.vaddr();
  }

  // For each pair of segment types S1 and S2, there is a call:
  //   bool Merge(V& storage, const S1& first, const S2& second);
  // where V is std::variant<S1,S2,...> (not necessarily in that order).  The
  // first and second arguments might or might not be references (aliases) into
  // storage.  If the first and second segments are adjacent and compatible,
  // this merges them by storing a new merged range into storage and then
  // returns true.
  template <class Merged, class... T, class First, class Second, typename... A>
  static constexpr void Emplace(std::variant<T...>& storage, const First& first,
                                const Second& second, A... args) {
    size_type memsz = first.memsz() + second.memsz();
    storage.template emplace<Merged>(first.offset(), first.vaddr(), memsz, args...);
  }

  // Helper used when details other than vaddr, offset, and memsz match.
  template <class... T, class First, typename... A>
  static constexpr bool MergeSame(std::variant<T...>& storage, const First& first,
                                  const First& second, A... args) {
    if (Adjacent(first, second)) {
      Emplace<First>(storage, first, second, args...);
      return true;
    }
    return false;
  }

  // This is the fallback overload for mismatched segment types, since this
  // is the base class of every segment but the actual type of none.
  template <class... T>
  static constexpr bool Merge(std::variant<T...>& storage, const SegmentBase& first,
                              const SegmentBase& second) {
    return false;
  }

  // Identical adjacent segments merge.
  template <class... T, PhdrLoadPolicy Policy>
  static constexpr bool Merge(std::variant<T...>& storage, const ConstantSegment<Policy>& first,
                              const ConstantSegment<Policy>& second) {
    return first.flags() == second.flags() && MergeSame(storage, first, second, first.flags());
  }

  // Identical adjacent segments merge.
  template <class... T, PhdrLoadPolicy Policy>
  static constexpr bool Merge(std::variant<T...>& storage, const DataSegment<Policy>& first,
                              const DataSegment<Policy>& second) {
    size_type filesz = first.filesz() + second.filesz();
    return MergeSame(storage, first, second, filesz);
  }

  // A data segment can be merged into an adjacent data + bss segment.
  template <class... T, PhdrLoadPolicy Policy>
  static constexpr bool Merge(std::variant<T...>& storage, const DataSegment<Policy>& first,
                              const DataWithZeroFillSegment<Policy>& second) {
    if (Adjacent(first, second)) {
      size_type filesz = first.filesz() + second.filesz();
      Emplace<DataWithZeroFillSegment<Policy>>(storage, first, second, filesz);
      return true;
    }
    return false;
  }

  // A data segment can be merged with an adjacent plain zero-fill segment.
  template <class... T, PhdrLoadPolicy Policy>
  static constexpr bool Merge(std::variant<T...>& storage, const DataSegment<Policy>& first,
                              const ZeroFillSegment& second) {
    if (Adjacent(first, second)) {
      Emplace<DataWithZeroFillSegment<Policy>>(storage, first, second, first.filesz());
      return true;
    }
    return false;
  }

  // All those add up to maybe merging any two adjacent segments.

  template <class... T, class First>
  static constexpr bool Merge(std::variant<T...>& storage, const First& first,
                              const std::variant<T...>& second) {
    return std::visit(
        [&storage, &first](const auto& second) { return Merge(storage, first, second); }, second);
  }

  template <class... T, class Second>
  static constexpr bool Merge(std::variant<T...>& storage, const std::variant<T...>& first,
                              const Second& second) {
    return std::visit(
        [&storage, &second](const auto& first) { return Merge(storage, first, second); }, first);
  }

  template <class... T>
  static constexpr bool Merge(std::variant<T...>& first, std::variant<T...>& second) {
    return std::visit(
        [&storage = first](const auto& first, const auto& second) {
          return Merge(storage, first, second);
        },
        first, second);
  }

  template <class... T, class Second>
  static constexpr bool Merge(std::variant<T...>& first, const Second& second) {
    return std::visit(
        [&storage = first, &second](const auto& first) { return Merge(storage, first, second); },
        first);
  }
};

}  // namespace elfldltl::internal

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_LOAD_SEGMENT_TYPES_H_
