// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_PIE_WITH_VDSO_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_PIE_WITH_VDSO_H_

#include <atomic>
#include <tuple>
#include <utility>

#include "diagnostics.h"
#include "dynamic.h"
#include "link.h"
#include "memory.h"
#include "relocation.h"
#include "self.h"
#include "symbol.h"

// This file implements self-relocation for a static PIE that can have limited
// symbolic relocations against a single vDSO module.
//
// The instantiation of the templates here must be statically linked into the
// startup code of the PIE.  It must be called before anything that uses any
// relocated data, including implicit GOT or PLT references--i.e. anything not
// explicitly declared with [[gnu::visibility("hidden")]]--or initialized data
// containing pointer values.
//
// This supports not only simple fixup but symbolic relocation too.  However
// this uses the most trivial symbol resolution rules: all symbolic relocations
// are presumed to use undefined symbols that must be resolved in the vDSO
// symbol table.

namespace elfldltl {

// Do self-relocation against the vDSO so system calls can be made normally.
// This is the simplified all-in-one version that decodes the all vDSO details
// from memory itself.  It returns the the program's own SymbolInfo data; see
// <lib/elfldltl/symbol.h> for details.
template <class Self, class DiagnosticsType>
inline SymbolInfo<typename Self::Elf> LinkStaticPieWithVdso(  //
    const Self& self, DiagnosticsType& diagnostics, const void* vdso_base);

// This version takes vDSO details already distilled separately.
template <class Self, class DiagnosticsType>
inline SymbolInfo<typename Self::Elf> LinkStaticPieWithVdso(
    const Self& self, DiagnosticsType& diagnostics,
    const SymbolInfo<typename Self::Elf>& vdso_symbols, typename Self::Elf::size_type vdso_bias) {
  using namespace std::literals;
  using Elf = typename Self::Elf;
  using size_type = typename Elf::size_type;
  using Sym = typename Elf::Sym;

  auto memory = Self::Memory();
  auto bias = static_cast<size_type>(Self::LoadBias());

  // Collect our own information.
  RelocationInfo<Elf> reloc_info;
  SymbolInfo<Elf> symbol_info;
  DecodeDynamic(diagnostics, memory, Self::Dynamic(),       //
                DynamicRelocationInfoObserver(reloc_info),  //
                DynamicSymbolInfoObserver(symbol_info));

  // Apply simple fixups first, just in case anything else needs them done.
  if (RelocateRelative(memory, reloc_info, bias)) {
    std::atomic_signal_fence(std::memory_order_seq_cst);
  } else [[unlikely]] {
    __builtin_trap();
  }

  // This communicates the results of a symbol lookup back to RelocateSymbolic.
  struct Definition {
    constexpr bool undefined_weak() const { return false; }

    constexpr const Sym& symbol() const { return symbol_; }

    constexpr size_type bias() const { return bias_; }

    // These will never actually be called.
    constexpr size_type tls_module_id() const { return 0; }
    constexpr size_type static_tls_bias() const { return 0; }
    constexpr size_type tls_desc_hook() const { return 0; }
    constexpr size_type tls_desc_value() const { return 0; }

    const Sym& symbol_;
    size_type bias_;
  };

  // Symbol resolution is trivial: it's defined in the vDSO (or we crash).
  auto resolve = [&](const auto& ref, RelocateTls tls_type)  //
      -> std::optional<Definition> {
    if (tls_type != RelocateTls::kNone) [[unlikely]] {
      diagnostics.FormatError("TLS relocations not supported in vDSO"sv);
      return std::nullopt;
    }
    SymbolName name(symbol_info, ref);
    const Sym* vdso_sym = name.Lookup(vdso_symbols);
    if (!vdso_sym) [[unlikely]] {
      diagnostics.FormatError("reference to symbol not defined in vDSO"sv, name);
      return std::nullopt;
    }
    return Definition{*vdso_sym, vdso_bias};
  };

  // Apply all the symbolic relocations, resolving each reference in the vDSO.
  if (RelocateSymbolic(memory, diagnostics, reloc_info, symbol_info, bias, resolve)) {
    std::atomic_signal_fence(std::memory_order_seq_cst);
  } else [[unlikely]] {
    __builtin_trap();
  }

  return symbol_info;
}

// This distills the vDSO symbols and load bias from the image in memory.
template <class Elf, class DiagnosticsType>
inline std::pair<SymbolInfo<Elf>, uintptr_t> GetVdsoSymbols(DiagnosticsType& diagnostics,
                                                            const void* vdso_base) {
  using Ehdr = typename Elf::Ehdr;
  using Phdr = typename Elf::Phdr;
  using Dyn = typename Elf::Dyn;

  SymbolInfo<Elf> vdso_symbols;

  DirectMemory vdso_image(
      {
          static_cast<std::byte*>(const_cast<void*>(vdso_base)),
          SIZE_MAX,
      },
      0);

  const Ehdr& vdso_ehdr = *vdso_image.ReadFromFile<Ehdr>(0);
  cpp20::span<const Phdr> vdso_phdrs = *vdso_image.ReadArrayFromFile<Phdr>(
      vdso_ehdr.phoff, NoArrayFromFile<Phdr>(), vdso_ehdr.phnum);

  constexpr uintptr_t kNoAddr = ~uintptr_t{};
  uintptr_t vdso_image_vaddr = kNoAddr;
  for (const auto& ph : vdso_phdrs) {
    if (ph.type == ElfPhdrType::kDynamic) {
      auto dyn = vdso_image.ReadArray<Dyn>(ph.vaddr(), ph.filesz() / sizeof(Dyn));
      if (!dyn) [[unlikely]] {
        diagnostics.FormatError("cannot read vDSO PT_DYNAMIC"sv);
        __builtin_trap();
      }
      DecodeDynamic(diagnostics, vdso_image, *dyn, DynamicSymbolInfoObserver(vdso_symbols));
    } else if (ph.type == ElfPhdrType::kLoad && vdso_image_vaddr == kNoAddr) {
      vdso_image_vaddr = ph.vaddr;
    }
  }
  if (vdso_image_vaddr == kNoAddr) [[unlikely]] {
    diagnostics.FormatError("no PT_LOAD found in vDSO"sv);
    __builtin_trap();
  }

  auto vdso_bias = reinterpret_cast<uintptr_t>(vdso_base) - vdso_image_vaddr;
  return {vdso_symbols, vdso_bias};
}

// This just combines the two functions above.
template <class Self, class DiagnosticsType>
inline SymbolInfo<typename Self::Elf> LinkStaticPieWithVdso(  //
    const Self& self, DiagnosticsType& diagnostics, const void* vdso_base) {
  using Elf = typename Self::Elf;

  // Fetch the vDSO symbol table.
  auto [vdso_symbols, vdso_bias] = GetVdsoSymbols<Elf>(diagnostics, vdso_base);

  // The main work is done in the overload defined above.
  return LinkStaticPieWithVdso(self, diagnostics, vdso_symbols, vdso_bias);
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_PIE_WITH_VDSO_H_
