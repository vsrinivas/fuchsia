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
    return add(DataSegment(offset, vaddr, memsz));
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

  Container<Segment> segments_;
  size_type vaddr_start_ = 0, vaddr_size_ = 0;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_
