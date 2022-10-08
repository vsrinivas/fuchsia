// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/stdcompat/span.h>
#include <lib/symbolizer-markup/writer.h>

#include <cstdint>
#include <string_view>

namespace elfldltl {

// This uses the writer object, some symbolizer_markup::Writer<...> type or
// equivalent, to write the symbolizer markup contextual elements describing a
// single ELF module.  This requires some elfldltl::LoadInfo<...> or equivalent
// object to describe its segments, as well as its markup module ID number (usually assigned
// monotonically increasing as new modules are loaded)
template <class Writer, class LoadInfoType>
constexpr void SymbolizerMarkupContext(Writer&& writer, std::string_view prefix,
                                       unsigned int module_id, std::string_view module_name,
                                       cpp20::span<const std::byte> build_id,
                                       LoadInfoType&& load_info,
                                       typename LoadInfoType::size_type load_bias) {
  auto markup_line = [prefix, &writer]() -> Writer& {
    if (!prefix.empty()) {
      writer.Prefix(prefix);
    }
    return writer;
  };

  markup_line().ElfModule(module_id, module_name, build_id);
  load_info.VisitSegments([&](const auto& segment) {
    const symbolizer_markup::MemoryPermissions permissions = {
        .read = segment.readable(),
        .write = segment.writable(),
        .execute = segment.executable(),
    };
    markup_line().LoadImageMmap(segment.vaddr() + load_bias, segment.memsz(), module_id,
                                permissions, segment.vaddr());
  });
}

}  // namespace elfldltl
