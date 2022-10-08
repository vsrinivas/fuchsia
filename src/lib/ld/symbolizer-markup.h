// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/elfldltl/link-map-list.h>
#include <lib/elfldltl/symbolizer-markup.h>
#include <lib/ld/elf.h>
#include <lib/ld/module.h>

#include "load-module.h"

namespace ld {

// This takes iterators for some Container<LoadModule> type (see load-module.h)
// and emits each module's symbolizer markup contextual elements.
template <class Writer, typename ModuleIterator>
constexpr void SymbolizerMarkupContext(Writer&& writer, std::string_view prefix,
                                       ModuleIterator first, ModuleIterator last) {
  while (first != last) {
    const auto& load_module = *first++;
    const std::string_view name = load_module->module.soname.str();
    const unsigned int module_id = load_module.module->symbolizer_modid;
    const cpp20::span build_id = load_module.module->build_id;
    const auto& info = load_module.load_info;
    const auto bias = load_module.load_bias();
    elfldltl::SymbolizerMarkupContext(writer, prefix, module_id, name, build_id, info, bias);
  }
}

// This takes some Container<LoadModule> type (see load-module.h) and emits
// each module's symbolizer markup contextual elements.
template <class Writer, class Modules>
constexpr void SymbolizerMarkupContext(Writer&& writer, std::string_view prefix,
                                       Modules&& modules) {
  SymbolizerMarkupContext(writer, prefix, modules.begin(), modules.end());
}

}  // namespace ld
