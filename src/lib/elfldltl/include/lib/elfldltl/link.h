// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LINK_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LINK_H_

// This file provides template APIs that do the central orchestration of
// dynamic linking: resolving and applying relocations.

#include "diagnostics.h"
#include "machine.h"
#include "relocation.h"

namespace elfldltl {

// Apply simple fixups as directed by Elf::RelocationInfo, given the load bias:
// the difference between runtime addresses and addresses that appear in the
// relocation records.  This calls memory.Store(reloc_address, runtime_address)
// or memory.StoreAdd(reloc_address, bias) to store the adjusted values.
// Returns false iff any calls into the Memory object returned false.
template <class Memory, class RelocInfo>
[[nodiscard]] constexpr bool RelocateRelative(Memory& memory, const RelocInfo& info,
                                              typename RelocInfo::size_type bias) {
  using Addr = typename RelocInfo::Addr;
  using size_type = typename RelocInfo::size_type;

  struct Visitor {
    // RELA entry with separate addend.
    constexpr bool operator()(const typename RelocInfo::Rela& reloc) const {
      auto addr = bias_ + reloc.addend();
      return memory_.template Store<Addr>(reloc.offset, addr);
    }

    // REL or RELR entry with addend in place.
    constexpr bool operator()(size_type addr) const {
      return memory_.template StoreAdd<Addr>(addr, bias_);
    }

    Memory& memory_;
    size_type bias_;
  };

  return info.VisitRelative(Visitor{memory, bias});
}

// Symbolic relocation for STT_TLS symbols requires the symbolic resolution
// engine meet different invariants depending on the specific relocation type.
enum class RelocateTls {
  kNone,     // Not TLS.
  kDynamic,  // Dynamic TLS reloc: the defining module will have a TLS segment.
  kStatic,   // Static TLS reloc: the defining module needs static TLS layout.
  kDesc,     // TLSDESC reloc: the definition must supply hook and parameter.
};

// Apply symbolic relocations as directed by Elf::RelocationInfo, referring to
// Elf::SymbolInfo as adjusted by the load bias (as used in RelocateRelative).
// The callback function is:
//
//  * std::optional<Definition> resolve(const Sym&, RelocateTls)
//
// where Definition is some type defined by the caller that supports methods:
//
//  * bool undefined_weak()
//    Returns true iff the symbol was resolved as an undefined weak reference.
//
//  * size_type bias()
//    Returns the load bias for symbol addresses in the defining module.
//
//  * const Sym& symbol()
//    Returns the defining symbol table entry.
//
//  * size_type tls_module_id()
//    Returns the TLS module ID number for the defining module.
//
//  * size_type static_tls_bias()
//    Returns the static TLS layout bias for the defining module.
//
//  * size_type tls_desc_hook(), tls_desc_value()
//    Returns the two values for the TLSDESC resolution.
//
template <ElfMachine Machine = ElfMachine::kNative, class Memory, class DiagnosticsType,
          class RelocInfo, class SymbolInfo, typename Resolve>
[[nodiscard]] constexpr bool RelocateSymbolic(Memory& memory, DiagnosticsType& diagnostics,
                                              const RelocInfo& reloc_info,
                                              const SymbolInfo& symbol_info,
                                              typename RelocInfo::size_type bias,
                                              Resolve&& resolve) {
  using namespace std::literals;

  using Addr = typename RelocInfo::Addr;
  using size_type = typename RelocInfo::size_type;
  using Rel = typename RelocInfo::Rel;
  using Rela = typename RelocInfo::Rela;

  static_assert(std::is_same_v<typename SymbolInfo::Addr, Addr>,
                "incompatible RelocInfo and SymbolInfo types passed to elfldltl::RelocateSymbolic");
  using Sym = typename SymbolInfo::Sym;

  static_assert(std::is_invocable_v<Resolve, const Sym&, RelocateTls>,
                "elfldltl::RelocateSymbolic requires resolve(const Sym&, RelocateTls) callback");

  using Traits = RelocationTraits<Machine>;
  using Type = typename Traits::Type;

  // Apply either a REL or RELA reloc resolved to a value, ignoring the addend.
  auto apply_no_addend = [&](const auto& reloc, size_type value) -> bool {
    return memory.template Store<Addr>(reloc.offset, value);
  };

  // Apply either a REL or RELA reloc resolved to a value, using the addend.
  auto apply_with_addend = [&](const auto& reloc, size_type value) -> bool {
    if constexpr (std::is_same_v<decltype(reloc), const Rel&>) {
      return memory.template StoreAdd<Addr>(reloc.offset, value);
    } else {
      static_assert(std::is_same_v<decltype(reloc), const Rela&>);
      return memory.template Store<Addr>(reloc.offset, value + reloc.addend());
    }
  };

  // Resolve a symbolic relocation and call apply(reloc, defn) to apply it to
  // the resolved Definition object (as `const&`), if successful.
  auto relocate_symbolic = [&](const auto& reloc, auto&& apply,
                               RelocateTls tls = RelocateTls::kNone) -> bool {
    const uint32_t symndx = reloc.symndx();
    if (symndx == 0) [[unlikely]] {
      return diagnostics.FormatError(
          "symbolic relocation entry uses reserved symbol table index 0"sv);
    }
    decltype(auto) symtab = symbol_info.symtab();
    if (symndx >= symtab.size()) [[unlikely]] {
      return diagnostics.FormatError("relocation entry symbol table index out of bounds"sv);
    }
    const Sym& sym = symtab[symndx];
    const bool is_tls = tls != RelocateTls::kNone;
    if ((sym.type() == ElfSymType::kTls) != is_tls) [[unlikely]] {
      return diagnostics.FormatError(  //
          is_tls ? "TLS relocation entry with non-STT_TLS symbol"sv
                 : "non-TLS relocation entry with STT_TLS symbol"sv);
    }
    if (auto defn = resolve(sym, tls)) {
      return apply(reloc, *defn);
    }
    return false;
  };

  // The Definition for a non-TLS reloc is applied by passing the biased symbol
  // value to the low-level apply_{no,with}_addend function.
  constexpr auto nontls = [](auto&& apply) {
    return [apply](const auto& reloc, const auto& defn) {
      if (defn.undefined_weak()) {
        return apply(reloc, 0);
      }
      return apply(reloc, defn.symbol().value() + defn.bias());
    };
  };

  // Static TLS relocs resolve to an offset from the thread pointer.  Each
  // module capable of resolving definitions for static TLS relocs will have
  // had a static TLS layout assigned.
  auto tls_absolute = [apply_with_addend](const auto& reloc, const auto& defn) {
    return apply_with_addend(reloc, defn.symbol().value() + defn.static_tls_bias());
  };

  // TLSMOD relocs resolve to a module ID (index), not an address value.
  // This is stored in a GOT slot to be passed to __tls_get_addr.
  auto tls_module = [apply_no_addend](const auto& reloc, const auto& defn) {
    return apply_no_addend(reloc, defn.tls_module_id());
  };

  // Dynamic TLS relocs resolve to an offset into the defining module's TLS
  // segment, stored in a GOT slot to be passed to __tls_get_addr.
  auto tls_relative = [apply_with_addend](const auto& reloc, const auto& defn) {
    return apply_with_addend(reloc, defn.symbol().value());
  };

  // Each TLSDESC reloc acts like two relocs to consecutive GOT slots: first
  // the hook function, which is passed the address of its own GOT slot; then a
  // stored value for the hook to retrieve.  The reloc's addend is applied only
  // to the value (for REL, stored in place in that second slot).  The resolver
  // selects an appropriate hook function for the defining module (static or
  // dynamic), and the encoding of the value is between the resolver and its
  // hook function (except that it must be some sort of byte offset in a TLS
  // block such that applying the addend here makes sense).
  auto tls_desc = [&](const auto& reloc1, const auto& defn) {
    auto reloc2 = reloc1;
    reloc2.offset = reloc1.offset + sizeof(size_type);
    return apply_no_addend(reloc1, defn.tls_desc_hook()) &&
           apply_with_addend(reloc2, defn.tls_desc_value());
  };

  // Apply a single relocation record by dispatching on its type.
  auto relocate = [&](const auto& reloc) -> bool {
    const uint32_t type = reloc.type();

    switch (static_cast<Type>(type)) {
      case Type::kNone:
        return diagnostics.FormatWarning("R_*_NONE relocation record encountered"sv);

      case Type::kRelative:
        return diagnostics.FormatWarning("R_*_RELATIVE relocation record not sorted properly"sv) &&
               apply_with_addend(reloc, bias);

      case Type::kAbsolute:
        return relocate_symbolic(reloc, nontls(apply_with_addend));

      case Type::kPlt:
        return relocate_symbolic(reloc, nontls(apply_no_addend));

      case Type::kTlsModule:
        return relocate_symbolic(reloc, tls_module, RelocateTls::kDynamic);

      case Type::kTlsAbsolute:
        return relocate_symbolic(reloc, tls_absolute, RelocateTls::kStatic);

      case Type::kTlsRelative:
        return relocate_symbolic(reloc, tls_relative, RelocateTls::kDynamic);
    }

    // These two are not in the enum because they don't exist on every machine.
    // So they are defined as std::optional<uint32_t> and may be std::nullopt,
    // which will make these tautological comparisons that get eliminated.

    if (type == Traits::kGot) {
      return relocate_symbolic(reloc, nontls(apply_no_addend));
    }

    if (type == Traits::kTlsDesc) {
      return relocate_symbolic(reloc, tls_desc, RelocateTls::kDesc);
    }

    return diagnostics.FormatError("unrecognized relocation type"sv);
  };

  return reloc_info.VisitSymbolic(relocate);
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LINK_H_
