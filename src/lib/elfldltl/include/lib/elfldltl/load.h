// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_

#include <array>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "constants.h"
#include "internal/load-segment-types.h"
#include "layout.h"
#include "phdr.h"

namespace elfldltl {

// Read the ELF file header (Ehdr) from an ELF file using the File API (see
// memory.h), and validate it for loading on this machine.
//
// The first template parameter gives the elfldltl::Elf<...> layout to use,
// i.e. usually elfldltl::Elf<> for native loading.  The rest are deduced.  If
// the optional final argument is passed, it can be std::nullopt to accept any
// machine or another ElfMachine value to match rather than the native machine.
//
// This returns the return value of calling file.ReadFromFile<Ehdr>,
// i.e. some type std::optional<E> where `const Ehdr& ehdr = <E object>;`
// works and the E object owns the storage ehdr points into.
template <class Elf, class Diagnostics, class File>
constexpr auto LoadEhdrFromFile(Diagnostics& diagnostics, File& file,
                                std::optional<ElfMachine> machine = ElfMachine::kNative)
    -> decltype(file.template ReadFromFile<typename Elf::Ehdr>(0)) {
  using namespace std::literals::string_view_literals;

  using Ehdr = typename Elf::Ehdr;

  if (auto read_ehdr = file.template ReadFromFile<Ehdr>(0)) [[likely]] {
    const Ehdr& ehdr = *read_ehdr;
    if (ehdr.Loadable(diagnostics, machine)) [[likely]] {
      return read_ehdr;
    }
  } else {
    diagnostics.FormatError("cannot read ELF file header"sv);
  }

  return {};
}

// Read the ELF file header (Ehdr) and program headers (Phdr) from an ELF file
// using the File API (see memory.h), and validate for loading on this machine.
//
// This just combines LoadEhdrFromFile and ReadPhdrsFromFile (see phdr.h).
// It returns std::optional<std::pair<E, P>> where LoadEhdrFromFile returns
// std::optional<E> and ReadPhdrsFromFile returns std::optional<P>.  e.g.,
// ```
//   auto headers = elfldltl::LoadHeadersFromFile<elfldltl::Elf<>>(...);
//   if (headers) {
//     auto [ehdr_owner, phdrs_owner] = *headers;
//     const Ehdr& ehdr = ehdr_owner;
//     const cpp20::span<const Phdr> phdrs = phdrs_owner;
//     ...
//   }
// ```
template <class Elf, class Diagnostics, class File, typename PhdrAllocator>
constexpr auto LoadHeadersFromFile(Diagnostics& diagnostics, File& file,
                                   PhdrAllocator&& phdr_allocator,
                                   std::optional<ElfMachine> machine = ElfMachine::kNative)
    -> decltype(std::make_optional(std::make_pair(
        *file.template ReadFromFile<typename Elf::Ehdr>(0),
        *file.template ReadArrayFromFile<typename Elf::Phdr>(0, phdr_allocator, 0)))) {
  if (auto read_ehdr = LoadEhdrFromFile<Elf>(diagnostics, file, machine)) [[likely]] {
    const typename Elf::Ehdr& ehdr = *read_ehdr;
    if (auto read_phdrs = ReadPhdrsFromFile(diagnostics, file,
                                            std::forward<PhdrAllocator>(phdr_allocator), ehdr)) {
      return std::make_pair(*std::move(read_ehdr), *std::move(read_phdrs));
    }
  }
  return std::nullopt;
}

// This does the same work as LoadHeadersFromFile, but handles different
// ELFCLASS (and optionally, ELFDATA) formats in the ELF file.  It returns
// false if the file is invalid, or else returns the result of invoking the
// callback as `bool(const Ehdr&, cpp20::span<const Phdr>)` for the particular
// Elf64<...> or Elf32<...> instantiation chosen.
//
// If the optional expected_data argument is provided, it can be std::nullopt
// to permit callbacks with either data format (byte order) as well as either
// class.  The final optional argument gives the machine architecture to match,
// and likewise can be std::nullopt to accept any machine.
template <class Diagnostics, class File, class PhdrAllocator, typename Callback>
constexpr bool WithLoadHeadersFromFile(Diagnostics& diagnostics, File& file,
                                       PhdrAllocator&& phdr_allocator, Callback&& callback,
                                       std::optional<ElfData> expected_data = ElfData::kNative,
                                       std::optional<ElfMachine> machine = ElfMachine::kNative) {
  using namespace std::literals::string_view_literals;

  using Ehdr64Lsb = typename Elf64<ElfData::k2Lsb>::Ehdr;
  using Ehdr64Msb = typename Elf64<ElfData::k2Msb>::Ehdr;

  // Below we'll call this with Elf<...>::Ehdr depending on the format.
  auto load_headers = [&](const auto& ehdr) -> bool {
    using Ehdr = std::decay_t<decltype(ehdr)>;
    using Elf = typename Ehdr::ElfLayout;
    using Phdr = typename Elf::Phdr;
    if (!ehdr.Loadable(diagnostics, machine)) [[unlikely]] {
      return false;
    }
    auto read_phdrs =
        ReadPhdrsFromFile(diagnostics, file, std::forward<PhdrAllocator>(phdr_allocator), ehdr);
    if (!read_phdrs) [[unlikely]] {
      return false;
    }
    cpp20::span<const Phdr> phdrs = *read_phdrs;
    return std::invoke(std::forward<Callback>(callback), ehdr, phdrs);
  };

  // Below we'll call this with Elf64<...>::Ehdr depending on the byte order.
  auto check_class = [&](const auto& probe_ehdr) -> bool {
    using ProbeEhdr = std::decay_t<decltype(probe_ehdr)>;
    using ProbeElf = typename ProbeEhdr::ElfLayout;
    using Ehdr32 = typename Elf32<ProbeElf::kData>::Ehdr;
    // If the EI_CLASS field is invalid, it doesn't matter which one we use
    // because it won't get past Valid() either way.
    return probe_ehdr.elfclass == ElfClass::k64
               ? load_headers(probe_ehdr)
               : load_headers(reinterpret_cast<const Ehdr32&>(probe_ehdr));
  };

  // Read an ELFCLASS64 header, which is larger.  We'll only examine the
  // e_ident fields that are common to all the header layouts until we've
  // determined the right Ehdr type to use.
  auto read_probe_ehdr = file.template ReadFromFile<Elf64<>::Ehdr>(0);
  if (!read_probe_ehdr) [[unlikely]] {
    diagnostics.FormatError("cannot read ELF file header"sv);
    return false;
  }
  const Elf64<>::Ehdr& probe_ehdr = *read_probe_ehdr;

  if (!expected_data) {
    // If the EI_DATA field is invalid, it doesn't matter which one we use
    // because won't get past Valid() either way.
    return probe_ehdr.elfdata == ElfData::k2Lsb
               ? check_class(reinterpret_cast<const Ehdr64Lsb&>(probe_ehdr))
               : check_class(reinterpret_cast<const Ehdr64Msb&>(probe_ehdr));
  }

  // The caller accepts only one byte order, so only use that instantiation.
  switch (*expected_data) {
    case ElfData::k2Lsb:
      return check_class(reinterpret_cast<const Ehdr64Lsb&>(probe_ehdr));
    case ElfData::k2Msb:
      return check_class(reinterpret_cast<const Ehdr64Msb&>(probe_ehdr));
  }
}

// elfldltl::LoadInfo<Elf, Container, ...> holds all the information an ELF
// loader needs to know.  It holds representations of the PT_LOAD segments
// in terms that matter to loading, using Container<Segment, ...>.  The
// number of PT_LOAD segments and segments().size() do not necessarily
// match exactly.  Rather, each Segment is a normalized loading step.
//
// Container is a vector-like template, such as from an adapter like
// elfldltl::StdContainer<std::vector>::Container (container.h) or a special
// implementation like elfldltl::StaticVector<N>::Container (static-vector.h).
// It must support emplace_back, emplace, and erase methods.
//
// Segment is just an alias for std::variant<...> of the several specific
// types.  All segment types have vaddr() and memsz(); most have offset() and
// filesz().  All these are normalized to whole pages.
//
//  * ConstantSegment is loaded directly from the file (or was relocated);
//    * It also has `bool readable()` and `bool executable()`.
//    * It has `bool relro()` that only arises after ApplyRelro(..., true).
//
//  * DataSegment is loaded from the file but writable (usually copy-on-write).
//
//  * DataWithZeroFillSegment is the same but has memsz() > filesz().
//    * The bytes past filesz() are zero-fill, not from the file.
//
//  * ZeroFillSegment has only vaddr() and memsz().
//
// The GetPhdrObserver method is used with elfldltl::DecodePhdrs (see phdr.h)
// to call AddSegment, which can also be called directly with a valid sequence
// of PT_LOAD segments.
//
// The RelroBounds function returns the normalized [start, end) of RELRO so
// mprotect can be applied there.  The returned LoadInfo::Region object
// also has size() and empty() for convenience.
//
// The ApplyRelro method uses this to adjust the segments for uses where
// relocation precedes loading, after all segments have been added.
//
// After adjustment, VisitSegments can be used to iterate over segments()
// using std::visit.
//
template <class Elf, template <typename> class Container,
          PhdrLoadPolicy Policy = PhdrLoadPolicy::kBasic>
class LoadInfo {
 private:
  using Types = internal::LoadSegmentTypes<typename Elf::size_type>;

 public:
  using size_type = typename Elf::size_type;
  using Region = typename Types::Region;
  using Phdr = typename Elf::Phdr;

  using ConstantSegment = typename Types::template ConstantSegment<Policy>;
  using DataSegment = typename Types::template DataSegment<Policy>;
  using DataWithZeroFillSegment = typename Types::template DataWithZeroFillSegment<Policy>;
  using ZeroFillSegment = typename Types::ZeroFillSegment;

  using Segment =
      std::variant<ConstantSegment, DataSegment, DataWithZeroFillSegment, ZeroFillSegment>;

  constexpr Container<Segment>& segments() { return segments_; }
  constexpr const Container<Segment>& segments() const { return segments_; }

  constexpr size_type vaddr_start() const { return vaddr_start_; }

  constexpr size_type vaddr_size() const { return vaddr_size_; }

  // Add a PT_LOAD segment.
  template <class Diagnostics>
  constexpr bool AddSegment(Diagnostics& diagnostics, size_type page_size, const Phdr& phdr) {
    // Merge with the last segment if possible, or else append a new one.
    // Types::Merge overloads match each specific type as it's created below.
    auto add = [this, &diagnostics](auto&& segment) -> bool {
      return (!segments_.empty() && Types::Merge(segments_.back(), segment)) ||
             segments_.emplace_back(diagnostics, internal::kTooManyLoads, segment);
    };

    // Normalize the file and memory bounds to whole pages.
    auto [offset, filesz] = PageBounds(page_size, phdr.offset, phdr.filesz);
    auto [vaddr, memsz] = PageBounds(page_size, phdr.vaddr, phdr.memsz);

    // Choose which type of segment this should be.

    if (memsz == 0) [[unlikely]] {
      return true;
    }
    if (!(phdr.flags() & Phdr::kWrite)) {
      return add(ConstantSegment(offset, vaddr, memsz, phdr.flags));
    }
    if (phdr.filesz == 0) {
      return add(ZeroFillSegment(vaddr, memsz));
    }
    if (phdr.memsz > phdr.filesz) {
      return add(DataWithZeroFillSegment(offset, vaddr, memsz, filesz));
    }
    return add(DataSegment(offset, vaddr, memsz, filesz));
  }

  // Get an ephemeral object to pass to elfldltl::DecodePhdrs.  The
  // returned observer object must not outlive this LoadInfo object.
  constexpr auto GetPhdrObserver(size_type page_size) {
    auto add_segment = [this, page_size](auto& diagnostics, const Phdr& phdr) {
      return this->AddSegment(diagnostics, page_size, phdr);
    };
    return GetPhdrObserver(page_size, add_segment);
  }

  // Iterate over segments() by calling std::visit(visitor, segment).
  // Return false the first time the visitor returns false.
  template <typename T>
  constexpr bool VisitSegments(T&& visitor) {
    return VisitAllOf(std::forward<T>(visitor), segments_);
  }

  template <typename T>
  constexpr bool VisitSegments(T&& visitor) const {
    return VisitAllOf(std::forward<T>(visitor), segments_);
  }

  // When loading before relocation, the RelroBounds() region can just be made
  // read-only in memory after relocation.
  static constexpr Region RelroBounds(const std::optional<Phdr>& relro, size_type page_size) {
    Region region;
    if (relro) {
      region.start = relro->vaddr;
      if (region.start & (page_size - 1)) {
        region.start = (region.start + page_size - 1) & -page_size;
      }
      region.end = (relro->vaddr + relro->memsz) & -page_size;
    }
    return region;
  }

  // Apply RELRO for loading after relocation, adjusting the segments to
  // promote RELRO to read-only.  If the merge_ro flag is true, then the RELRO
  // segment can be merged with an adjacent true read-only segment.  This is
  // appropriate if relocations have been applied in place to the same image;
  // whether merged or not, the RELRO segment will be part of a ConstantSegment
  // with relro() false.  If already-relocated segments are stored separately
  // from true read-only segments (such as in a COW mirror of the original
  // portion of the file), the RELRO segment instead becomes a ConstantSegment
  // with relro() true.
  template <class Diagnostics>
  constexpr bool ApplyRelro(Diagnostics& diagnostics, const std::optional<Phdr>& relro,
                            size_type page_size, bool merge_ro) {
    using namespace std::literals::string_view_literals;

    constexpr std::string_view kMissing = "PT_GNU_RELRO not in any data segment";

    const Region region = RelroBounds(relro, page_size);
    if (region.empty()) {
      return true;
    }

    auto is_relro = [region](const auto& segment) {
      return region.start >= segment.vaddr() && region.end <= segment.vaddr() + segment.memsz();
    };

    for (auto it = segments().begin(); it != segments().end(); ++it) {
      Segment& segment = *it;

      // This does the same work in two different instantiations for
      // DataSegment and DataWithZeroFillSegment.
      auto check_relro = [&](auto& data) -> std::optional<bool> {
        // There is only one RELRO region and segments are in ascending order.
        // If we've passed it, there isn't one to find.
        if (data.vaddr() >= region.end) [[unlikely]] {
          return diagnostics.FormatError(kMissing);
        }

        if (data.vaddr() + data.memsz() <= region.start) {
          return std::nullopt;  // Keep looking.
        }

        if (region.start > data.vaddr()) [[unlikely]] {
          return diagnostics.FormatError("PT_GNU_RELRO not at segment start"sv);
        }

        // This is the segment containing RELRO.  Passing both it and data is
        // here redundant in that they're the same underlying Segment pointer;
        // but this dispatches to the correct FixupRelro instantiation for the
        // two data segment types.
        return FixupRelro(diagnostics, it, data, region.size(), merge_ro);
      };

      std::optional<bool> done;
      if (auto* data = std::get_if<DataSegment>(&segment)) {
        done = check_relro(*data);
      } else if (auto* bss = std::get_if<DataWithZeroFillSegment>(&segment)) {
        done = check_relro(*bss);
      } else if (diagnostics.extra_checking() && std::visit(is_relro, segment)) {
        return diagnostics.FormatError("PT_GNU_RELRO applied to non-data segment"sv);
      }
      if (done) {
        return *done;
      }
    }

    return diagnostics.FormatError(kMissing);
  }

 private:
  // Making this static with a universal reference parameter avoids having to
  // repeat the actual body in the const and non-const methods that call it.
  template <typename T, class C>
  static constexpr bool VisitAllOf(T&& visitor, C&& container) {
    for (auto& elt : container) {
      if (!std::visit(visitor, elt)) {
        return false;
      }
    }
    return true;
  }

  static constexpr std::pair<size_type, size_type> PageBounds(size_type page_size, size_type start,
                                                              size_type size) {
    size_type end = (start + size + page_size - 1) & -page_size;
    start &= -page_size;
    return {start, end - start};
  }

  template <typename T>
  constexpr auto GetPhdrObserver(size_type page_size, T&& add_segment) {
    return MakePhdrLoadObserver<Elf, Policy>(page_size, vaddr_start_, vaddr_size_,
                                             std::forward<T>(add_segment));
  }

  template <class SegmentType>
  constexpr std::pair<ConstantSegment, std::optional<Segment>> SplitRelro(
      const SegmentType& segment, size_type relro_size, bool merge_ro) {
    ConstantSegment relro_segment{
        segment.offset(),
        segment.vaddr(),
        relro_size,
        Phdr::kRead | (merge_ro ? 0 : static_cast<uint32_t>(Phdr::kWrite)),
    };

    assert(relro_size <= segment.memsz());
    if (relro_size == segment.memsz()) {
      return {relro_segment, {}};
    }

    auto offset = segment.offset() + relro_size;
    auto vaddr = segment.vaddr() + relro_size;
    auto memsz = segment.memsz() - relro_size;
    auto filesz = segment.filesz() - relro_size;

    if (std::is_same_v<SegmentType, DataSegment>) {
      return {relro_segment, DataSegment{offset, vaddr, memsz, filesz}};
    }

    assert((std::is_same_v<SegmentType, DataWithZeroFillSegment>));
    if (segment.filesz() - relro_size == 0) {
      return {relro_segment, ZeroFillSegment{vaddr, memsz}};
    }
    return {relro_segment, DataWithZeroFillSegment{offset, vaddr, memsz, filesz}};
  }

  // Complete ApplyRelro once the specific segment has been found.  This is
  // instantiated separately for DataSegment and DataWithZeroFillSegment.
  // It firsts creates a new ConstantSegment spanning relro_size.  If relro_size
  // is less than the full segment size a second segment will be inserted after
  // the relro segment.  Merging is attempted on the two new segments.  Note that
  // because the two new segments will never be mergeable together we only need
  // to try merging the relro segment with the segment behind it, and the split
  // segment with the one in front of it.  AddSegment will always merge if
  // possible, so no new merging opportunities will become available after those
  // two attempted merges have taken place.
  // This is useful in cases where the segments will be copied after applying relro
  // like in out of process dynamic linking. This can reduce the number of segments
  // that need to be copied over into that processes address space. It doesn't make
  // sense for traditional dynamic linking because all segments have already been
  // mapped in before relocation can take place, so there is no efficiency to be
  // found using this.
  template <class Diagnostics, class SegmentType>
  constexpr bool FixupRelro(Diagnostics& diagnostics, typename Container<Segment>::iterator it,
                            SegmentType segment, size_type relro_size, bool merge_ro) {
    auto [relro_segment, split_segment] = SplitRelro(segment, relro_size, merge_ro);

    auto merge = [this](auto it1, auto it2) {
      if (!Types::Merge(*it1, *it2)) {
        return it2;
      }
      // Distances are used in place of iterators because they can be invalidated
      // by insert/erase.
      auto dist = std::distance(segments().begin(), it1);
      segments().erase(it2);
      return segments().begin() + dist;
    };

    // Replace the current segment instead of erase + insert.
    *it = relro_segment;

    if (it != segments().begin()) {
      it = merge(it - 1, it);
    }

    if (split_segment) {
      it += 1;
      auto it_or_err = segments().emplace(diagnostics, internal::kTooManyLoads, it, *split_segment);
      if (!it_or_err) {
        return false;
      }
      it = *it_or_err;
    }

    if (it + 1 < segments().end()) {
      merge(it, it + 1);
    }

    return true;
  }

  Container<Segment> segments_;
  size_type vaddr_start_ = 0, vaddr_size_ = 0;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_
