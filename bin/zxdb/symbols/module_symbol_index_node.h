// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "llvm/DebugInfo/DWARF/DWARFDie.h"

namespace llvm {
class DWARFContext;
}

namespace zxdb {

// One node in the ModuleSymbolIndex tree. One node represents the set of things
// with the same name inside a given namespace of a module. The index contains
// things that might need to be named globally: functions, globals, and class
// statics. It does not contain function-level statics. Variables in functions
// are searched when in the context of that function, and can't be referenced
// outside of it.
//
// There could be multiple types of things with the same name in different
// compilation unit, and a single function can have multiple locations. So one
// one can represent many namespaces and functions.
class ModuleSymbolIndexNode {
 public:
  // A reference to a DIE that doesn't need the unit or the underlying
  // llvm::DwarfDebugInfoEntry to be kept. This allows the discarding of the
  // full parsed DIE structures after indexing. It can be converted back to a
  // DIE, which will cause the DWARFUnit to be re-parsed.
  //
  // The offset stored in this structure is the offset from the beginning of
  // the .debug_info section, which is the same as the offset stored in the
  // llvm::DWARFDebugInfoEntry.
  class DieRef {
   public:
    explicit DieRef() : offset_(0) {}
    explicit DieRef(uint32_t offset) : offset_(offset) {}
    explicit DieRef(const llvm::DWARFDie& die) : offset_(die.getOffset()) {}

    uint32_t offset() const { return offset_; }

    llvm::DWARFDie ToDie(llvm::DWARFContext* context) const;

   private:
    uint32_t offset_;
  };

  ModuleSymbolIndexNode();

  // Makes a node pointing to one DIE.
  ModuleSymbolIndexNode(const DieRef& ref);

  ~ModuleSymbolIndexNode();

  bool empty() const { return sub_.empty() && dies_.empty(); }

  const std::map<std::string, ModuleSymbolIndexNode>& sub() const {
    return sub_;
  }
  const std::vector<DieRef>& dies() const { return dies_; }

  // Dump DIEs for debugging. A node does not contain its own name (this
  // is stored in the parent's map. If printing some node other than the root,
  // specify the name.
  void Dump(std::ostream& out, int indent_level = 0) const;
  void Dump(const std::string& name, std::ostream& out,
            int indent_level = 0) const;

  // AsString is useful only in small unit tests since even a small module can
  // have many megabytes of dump.
  std::string AsString(int indent_level = 0) const;

  // Adds a DIE with the name of this node.
  void AddDie(const DieRef& ref);

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

  // For any DIES matching this name, lists the DIEs that implement it.
  // If a function or static variable has the same name as a namespace, there
  // could be sub_ entries as well as dies_.
  std::vector<DieRef> dies_;
};

}  // namespace zxdb
