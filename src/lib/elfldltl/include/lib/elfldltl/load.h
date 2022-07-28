// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_

#include <functional>
#include <optional>
#include <utility>

#include "constants.h"
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
                                   ElfMachine machine = ElfMachine::kNative)
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

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LOAD_H_
