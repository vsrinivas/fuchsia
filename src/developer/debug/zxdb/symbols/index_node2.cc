// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index_node2.h"

#include <sstream>

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

void MergeMaps(IndexNode2::Map& from, IndexNode2::Map* to) {
  for (auto& pair : from) {
    auto found = to->find(pair.first);
    if (found == to->end())
      to->insert(std::move(pair));
    else
      found->second.Merge(std::move(pair.second));
  }
}

void DumpMap(const IndexNode2::Map& map, int indent, const char* heading, std::ostream& out) {
  if (map.empty())
    return;

  out << std::string(indent * 2, ' ') << heading << std::endl;
  for (const auto& cur : map)
    cur.second.Dump(cur.first, out, indent + 1);
}

}  // namespace

llvm::DWARFDie IndexNode2::DieRef::ToDie(llvm::DWARFContext* context) const {
  return context->getDIEForOffset(offset_);
}

IndexNode2* IndexNode2::AddChild(Kind kind, const char* name, const DieRef& ref) {
  FXL_DCHECK(name);

  Map* map = nullptr;
  switch (kind) {
    case Kind::kNone:
    case Kind::kRoot:
      FXL_NOTREACHED();  // Can't add these.
      return nullptr;
    case Kind::kNamespace:
      map = &namespaces_;
      break;
    case Kind::kType:
      map = &types_;
      break;
    case Kind::kFunction:
      map = &functions_;
      break;
    case Kind::kVar:
      map = &vars_;
      break;
  }

  // TODO(brettw) Get some kind of transparent lookup here to avoid making an intermediate
  // std::string.
  auto found = map->find(name);
  if (found == map->end()) {
    found = map->emplace(std::piecewise_construct, std::forward_as_tuple(name),
                         std::forward_as_tuple(kind))
                .first;
  }
  found->second.AddDie(ref);
  return &found->second;
}

void IndexNode2::AddDie(const DieRef& ref) {
  switch (kind_) {
    case Kind::kNone:
    case Kind::kRoot:
      FXL_NOTREACHED() << "Should not try to add a none or root DIE.";
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
      break;  // Always store functions and variables.
  }

  dies_.push_back(ref);
}

std::string IndexNode2::AsString(int indent_level) const {
  std::ostringstream out;
  Dump(out, indent_level);
  return out.str();
}

void IndexNode2::Dump(std::ostream& out, int indent_level) const {
  DumpMap(namespaces_, indent_level + 1, "Namespaces:", out);
  DumpMap(types_, indent_level + 1, "Types:", out);
  DumpMap(functions_, indent_level + 1, "Functions:", out);
  DumpMap(vars_, indent_level + 1, "Variables:", out);
}

void IndexNode2::Dump(const std::string& name, std::ostream& out, int indent_level) const {
  out << std::string(indent_level * 2, ' ');
  if (name.empty())
    out << "<<empty index string>>";
  else
    out << name;
  out << std::endl;
  Dump(out, indent_level);
}

void IndexNode2::Merge(IndexNode2&& other) {
  FXL_DCHECK(kind_ == other.kind_);

  MergeMaps(other.namespaces_, &namespaces_);
  MergeMaps(other.types_, &types_);
  MergeMaps(other.functions_, &functions_);
  MergeMaps(other.vars_, &vars_);

  if (!other.dies_.empty()) {
    if (dies_.empty()) {
      dies_ = std::move(other.dies_);
    } else {
      // AddDie will apply de-duplication logic.
      for (const auto& cur : other.dies_)
        AddDie(cur);
    }
  }
}

}  // namespace zxdb
