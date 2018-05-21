// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace zxdb {

// One node in the ModuleSymbolIndex tree. One node represents the set of things
// with the same name inside a given namespace of a module. There could
// be multiple types of things with the same name in different compilation unit,
// and a single function can have multiple locations. So one one can represent
// many namespaces and functions.
class ModuleSymbolIndexNode {
 public:
  ModuleSymbolIndexNode();

  // Makes a node pointing to one function.
  ModuleSymbolIndexNode(const llvm::DWARFDie& die);

  ~ModuleSymbolIndexNode();

  bool empty() const { return sub_.empty() && function_dies_.empty(); }

  const std::map<std::string, ModuleSymbolIndexNode>& sub() const {
    return sub_;
  }
  const std::vector<llvm::DWARFDie>& function_dies() const {
    return function_dies_;
  }

  // Dump functions for debugging. A node does not contain its own name (this
  // is stored in the parent's map. If printing some node other than the root,
  // specify the name.
  void Dump(std::ostream& out, int indent_level = 0) const;
  void Dump(const std::string& name, std::ostream& out,
            int indent_level = 0) const;

  // AsString is useful only in small unit tests since even a small module can
  // have many megabytes of dump.
  std::string AsString(int indent_level = 0) const;

  // Adds a DIE for a function with the name of this node.
  void AddFunctionDie(const llvm::DWARFDie& die);

  // Adds a child node with the given name and returns it. If one already exits
  // with the name, returns the existing one.
  ModuleSymbolIndexNode* AddChild(std::string&& name);

  // Adds a child to this node. If a node with this key already exists in this
  // node, they will be merged.
  void AddChild(std::pair<std::string, ModuleSymbolIndexNode>&& child);

  // Merges another node's children into this one. It's assumed there are no
  // duplicate DIEs so the lists are just appended.
  void Merge(ModuleSymbolIndexNode&& other);

 private:
  // Performance note: The strings are all null-terminated C strings that come
  // from the mapped DWARF data. We should use that in the map instead to avoid
  // copying all the strings again.
  std::map<std::string, ModuleSymbolIndexNode> sub_;

  // For any functions matching this name, lists the DIEs that implement it.
  // If a function has the same name as a namespace, there could be sub_
  // entries as well as function_dies_.
  std::vector<llvm::DWARFDie> function_dies_;
};

}  // namespace zxdb
