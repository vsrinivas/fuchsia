// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/module_symbol_index_node.h"

#include <sstream>

#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

llvm::DWARFDie ModuleSymbolIndexNode::DieRef::ToDie(
    llvm::DWARFContext* context) const {
  return context->getDIEForOffset(offset_);
}

ModuleSymbolIndexNode::ModuleSymbolIndexNode() = default;

ModuleSymbolIndexNode::ModuleSymbolIndexNode(const DieRef& ref) {
  dies_.push_back(ref);
}

ModuleSymbolIndexNode::~ModuleSymbolIndexNode() = default;

void ModuleSymbolIndexNode::Dump(std::ostream& out, int indent_level) const {
  // When printing the root node, only do the children.
  for (const auto& cur : sub_)
    cur.second.Dump(cur.first, out, indent_level);
}

void ModuleSymbolIndexNode::Dump(const std::string& name, std::ostream& out,
                                 int indent_level) const {
  out << std::string(indent_level * 2, ' ') << name;
  if (!dies_.empty())
    out << " (" << dies_.size() << ")";
  out << std::endl;
  for (const auto& cur : sub_)
    cur.second.Dump(cur.first, out, indent_level + 1);
}

std::string ModuleSymbolIndexNode::AsString(int indent_level) const {
  std::ostringstream out;
  Dump(out, indent_level);
  return out.str();
}

void ModuleSymbolIndexNode::AddDie(const DieRef& ref) {
  if (ref.type() == RefType::kNamespace) {
    // Just save a namespace once.
    for (auto& existing : dies_) {
      if (existing.type() == RefType::kNamespace)
        return;  // Already have an entry for this namespace.
    }
  } else if (ref.type() == RefType::kType ||
             ref.type() == RefType::kTypeDecl) {
    // This is a type. Types only appear in the index once (see the class comment
    // in the header). This loop does the de-duplication and also upgrades
    // declarations to full definitions.
    for (auto& existing : dies_) {
      if (existing.type() == RefType::kTypeDecl) {
        if (ref.type() == RefType::kType) {
          // Upgrade existing declaration to full type.
          existing = ref;
        }
        // "Else" means they're both declarations, don't need to duplicate.
        return;
      } else if (existing.type() == RefType::kType) {
        // Already have a full type definition for this name, don't same.
        return;
      }
    }
  }

  // Add the new entry.
  dies_.push_back(ref);
}

ModuleSymbolIndexNode* ModuleSymbolIndexNode::AddChild(std::string&& name) {
  return &sub_.emplace(std::piecewise_construct,
                       std::forward_as_tuple(std::move(name)),
                       std::forward_as_tuple())
              .first->second;
}

void ModuleSymbolIndexNode::AddChild(
    std::pair<std::string, ModuleSymbolIndexNode>&& child) {
  auto existing = sub_.find(child.first);
  if (existing == sub_.end())
    sub_.emplace(std::move(child));
  else
    existing->second.Merge(std::move(child.second));
}

void ModuleSymbolIndexNode::Merge(ModuleSymbolIndexNode&& other) {
  for (auto& pair : other.sub_) {
    auto found = sub_.find(pair.first);
    if (found == sub_.end()) {
      sub_.insert(std::move(pair));
    } else {
      found->second.Merge(std::move(pair.second));
    }
  }

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
