// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/elfldltl/load.h>
#include <lib/elfldltl/relocation.h>
#include <lib/elfldltl/soname.h>
#include <lib/ld/elf.h>
#include <lib/ld/module.h>

#include <functional>

namespace ld {

// This is a temporary data structure that lives only while a module is being
// loaded and relocated.  It points to the permanent ld::abi::Module object.
//
// The first template parameter is used with elfldltl::LoadInfo as the
// container (template) for each module's segments (see <lib/elfldltl/load.h>).
//
// The remaining template parameters can be some sort of Listable<Class> type
// base class templates to make the LoadModule object an element of an
// intrustive container type (and possibly separately in multiple containers).
template <template <typename> class SegmentContainer, template <class> class... ModuleContainable>
struct LoadModule : public ModuleContainable<LoadModule<SegmentContainer, ModuleContainable>>... {
  using LoadInfo = elfldltl::LoadInfo<abi::Elf, SegmentContainer>;
  using RelocationInfo = elfldltl::RelocationInfo<abi::Elf>;

  // This is the API contract used by fbl::HashTable.
  static constexpr uint32_t GetHash(const LoadModule& load_module) {
    return load_module.module->soname.hash();
  }

  constexpr abi::Elf::size_type load_bias() const { return module->link_map.addr; }

  // This holds SymbolInfo, InitFiniInfo, etc. that are needed at runtime.
  // It's allocated separately so it can survive at runtime after this
  // LoadModule object is discarded.
  abi::Module* module = nullptr;

  // This describes the segments which are being, or just have been, loaded.
  LoadInfo load_info;

  // This describes relocations that need to be applied after loading and
  // before finalizing segment permissions (i.e. RELRO).
  RelocationInfo reloc_info;
};

// InlineLoadModule can be used when the abi::Module does not need to be
// allocated separately.  For example, if the load and relocation details are
// being cached for reuse.  In such a case, the abi::Module would be acting as
// a prototype with an artificial (or zero) load_bias() and internal pointers
// needing relocation to instantiate the module for a particular dynamic
// linking namespace and load address.
template <template <typename> class SegmentContainer, template <class> class... ModuleContainable>
class InlineLoadModule : public LoadModule<SegmentContainer, ModuleContainable...> {
 public:
  using Base = LoadModule<SegmentContainer, ModuleContainable...>;

  using Base::Base;

  constexpr InlineLoadModule() { this->module = &inline_module_; }

 private:
  abi::Module inline_module_;
};

}  // namespace ld

// This is the API contract for standard C++ hash-based containers.
template <template <typename> class SegmentContainer, template <class> class... ModuleContainable>
struct std::hash<ld::LoadModule<SegmentContainer, ModuleContainable...>> {
  constexpr uint32_t operator()(
      const ld::LoadModule<SegmentContainer, ModuleContainable...>& load_module) {
    return load_module.module->soname.hash();
  }
};
