// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace llvm {
class DWARFContext;
class DWARFDie;
}  // namespace llvm

namespace zxdb {

// One node in the ModuleSymbolIndex tree. One node represents the set of
// things with the same name inside a given named scope (namespace, class,
// type, etc.) of a module. The index contains things that might need to be
// named globally: types, functions, globals, and class statics. It does not
// contain function-level statics. Variables in functions are searched when in
// the context of that function, and can't be referenced outside of it.
//
// There could be multiple types of things with the same name in different
// compilation unit, and a single function can have multiple locations. So one
// one can represent many namespaces and functions.
//
// DUPLICATE TYPE HANDLING
// -----------------------
// We assume there is only one definition for a given type name. Usually there
// will be one definition of a type in each compilation unit it appears in, so
// there is epic duplication of common type definitions in each module
// (covering many compilation units).
//
// The duplications aren't necessarily the same since the programmer could have
// multiple different types with the same names in different contexts.
// Depending on how things are linked, the user may not even notice
// (technically violating the "one definition rule" leads to undefined
// behavior, not failure).
//
// The main time this will come up is types defined in anonymous namespaces
// which can easily be legally duplicated. For this, we need specific
// disambiguation for anonymous namespaces associated with a given file. Once
// we can express the difference between different anonymous namespaces, these
// will no longer collide without having to do special handling in this
// function.
//
// We do want to upgrade forward-declarations to full definitions when we
// find them. Some compilation units won't even have full definitions for
// a type they use (say when a pointer is passed through a file without being
// dereferenced). Therefore, "types" will overwrite "type declarations."
//
// NAMESPACE HANDLING
// ------------------
// Namespaces are de-duplicated, with only one DIE saved per namespace name.
// This means that one won't be able to enumerate the contents of a namespace
// with the symbol returned from the index. This is because currently we only
// need to know that a namespace exists with that name, not exactly where all
// of its declarations are.
class ModuleSymbolIndexNode {
 private:
  using Map = std::map<std::string, ModuleSymbolIndexNode>;

 public:
  using Iterator = Map::iterator;
  using ConstIterator = Map::const_iterator;

  // Type for a DieRef.
  enum class RefType {
    kNamespace,  // Namespaces.
    kFunction,   // Any kind of code.
    kVariable,   // Any kind of data.
    kTypeDecl,   // Forward declaration of a type.
    kType,       // Full type definition.
  };

  // A reference to a DIE that doesn't need the unit or the underlying
  // llvm::DwarfDebugInfoEntry to be kept. This allows the discarding of the
  // full parsed DIE structures after indexing. It can be converted back to a
  // DIE, which will cause the DWARFUnit to be re-parsed.
  //
  // The offset stored in this structure is the offset from the beginning of
  // the .debug_info section, which is the same as the offset stored in the
  // llvm::DWARFDebugInfoEntry.
  //
  // Random code reading the index can convert a DieRef to a Symbol object
  // using ModuleSymbols::IndexDieRefToSymbol().
  class DieRef {
   public:
    explicit DieRef() : offset_(0) {}
    DieRef(RefType type, uint32_t offset) : type_(type), offset_(offset) {}

    RefType type() const { return type_; }
    uint32_t offset() const { return offset_; }

    // For use by ModuleSymbols. Other callers read the DieRef comments above.
    llvm::DWARFDie ToDie(llvm::DWARFContext* context) const;

   private:
    RefType type_;
    uint32_t offset_;
  };

  ModuleSymbolIndexNode();

  // Makes a node pointing to one DIE.
  explicit ModuleSymbolIndexNode(const DieRef& ref);

  ~ModuleSymbolIndexNode();

  bool empty() const { return sub_.empty() && dies_.empty(); }

  const Map& sub() const { return sub_; }
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

  // Finds the first child node that contains the input string a prefix. The
  // first return iterator points to this node.
  //
  // The second returned iterator points to the last node IN THE CONTAINER.
  // This does not indicate the last node with the prefix. Many callers won't
  // need all of the matches and doing it this way avoids a second lookup.
  //
  // If there are no matches both iterators will be the same (found == end).
  //
  // If the caller wants to find all matching prefixes, it can advance the
  // iterator as long as the last input component is a prefix if the current
  // iterator key and less than the end.
  std::pair<ModuleSymbolIndexNode::ConstIterator,
            ModuleSymbolIndexNode::ConstIterator>
  FindPrefix(const std::string& input) const;

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
