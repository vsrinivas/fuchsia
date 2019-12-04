// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/elf_symbol.h"

namespace zxdb {

std::string ElfSymbol::ComputeFullName() const {
  // See ComputeIdentifier below for some of the complexity for these symbols.
  return record_.unmangled_name;
}

Identifier ElfSymbol::ComputeIdentifier() const {
  // For now, throw everything into the first identifier component. Many of the ELF symbols are
  // not simple identifiers, and the parent mess up our identifier parsing.
  // TODO(bug 41928) fix identifier parsing when there are function parameter types.
  //
  // Some examples:
  // clang-format off
  //  - "vtable for debug_agent::DebugAgent"
  //  - "virtual thunk to std::__2::basic_istream<char, std::__2::char_traits<char> >::~basic_istream()"
  //  - "(anonymous namespace)::TransformerBase::TransformString((anonymous namespace)::Position const&, (anonymous namespace)::TraversalResult*)::string_as_coded_vector"
  // clang-format on
  return Identifier(IdentifierComponent(record_.unmangled_name));
}

}  // namespace zxdb
