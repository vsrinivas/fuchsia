// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index_node.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/symbol_factory.h"

namespace zxdb {

namespace {

void DumpMap(const IndexNode::Map& map, int indent, const char* heading,
             SymbolFactory* factory_for_loc, std::ostream& out) {
  if (map.empty())
    return;

  out << std::string(indent * 2, ' ') << heading << std::endl;
  for (const auto& cur : map)
    cur.second.Dump(cur.first, out, factory_for_loc, indent + 1);
}

}  // namespace

IndexNode* IndexNode::AddChild(Kind kind, const char* name) {
  FX_DCHECK(name);

  // TODO(brettw) Get some kind of transparent lookup here to avoid making an intermediate
  // std::string.
  Map& map = MapForKind(kind);
  auto found = map.find(name);
  if (found == map.end()) {
    found = map.emplace(std::piecewise_construct, std::forward_as_tuple(name),
                        std::forward_as_tuple(kind))
                .first;
  }

  return &found->second;
}

IndexNode* IndexNode::AddChild(Kind kind, const char* name, const SymbolRef& ref) {
  auto added = AddChild(kind, name);
  added->AddDie(ref);
  return added;
}

void IndexNode::AddDie(const SymbolRef& ref) {
  switch (kind_) {
    case Kind::kNone:
    case Kind::kRoot:
      FX_NOTREACHED() << "Should not try to add a none or root DIE.";
      return;
    case Kind::kNamespace:
      // Don't bother saving namespaces.
      return;
    case Kind::kType:
      // A type can only have one entry. If it's a forward declaration, we'll promote it to a
      // definition. But otherwise won't append.
      if (!dies_.empty()) {
        if (!dies_[0].is_declaration())
          return;  // Existing one is already a definition, never need another.
        else if (ref.is_declaration())
          return;       // Both existing one and new one are definitions, don't need to upgrade.
        dies_.clear();  // Update existing one by removing, will be appended below.
      }
      break;
    case Kind::kFunction:
    case Kind::kVar:
      break;  // Always store these kinds.
  }

  dies_.push_back(ref);
}

const IndexNode::Map& IndexNode::MapForKind(Kind kind) const {
  FX_DCHECK(static_cast<int>(kind) >= 0 &&
            static_cast<int>(kind) < static_cast<int>(Kind::kEndPhysical));
  return children_[static_cast<int>(kind)];
}

IndexNode::Map& IndexNode::MapForKind(Kind kind) {
  FX_DCHECK(static_cast<int>(kind) >= 0 &&
            static_cast<int>(kind) < static_cast<int>(Kind::kEndPhysical));
  return children_[static_cast<int>(kind)];
}

std::string IndexNode::AsString(int indent_level) const {
  std::ostringstream out;
  Dump(out, nullptr, indent_level);
  return out.str();
}

void IndexNode::Dump(std::ostream& out, SymbolFactory* factory_for_loc, int indent_level) const {
  DumpMap(namespaces(), indent_level + 1, "Namespaces:", factory_for_loc, out);
  DumpMap(types(), indent_level + 1, "Types:", factory_for_loc, out);
  DumpMap(functions(), indent_level + 1, "Functions:", factory_for_loc, out);
  DumpMap(vars(), indent_level + 1, "Variables:", factory_for_loc, out);
}

void IndexNode::Dump(const std::string& name, std::ostream& out, SymbolFactory* factory_for_loc,
                     int indent_level) const {
  out << std::string(indent_level * 2, ' ');
  if (name.empty())
    out << "<<empty index string>>";
  else
    out << name;

  if (factory_for_loc) {
    // Dump location information too.
    const char* separator = ": ";
    for (const SymbolRef& die_ref : dies_) {
      out << separator;
      separator = ", ";

      LazySymbol lazy = factory_for_loc->MakeLazy(die_ref.offset());
      const Symbol* symbol = lazy.Get();
      if (const Function* function = symbol->As<Function>()) {
        out << function->code_ranges().ToString();
      } else {
        // Everything else just gets the DIE offset so we can identify it. This can be customized
        // in the future if needed.
        out << std::hex << "0x" << die_ref.offset();
      }
    }
  }

  out << std::endl;
  Dump(out, factory_for_loc, indent_level);
}

}  // namespace zxdb
