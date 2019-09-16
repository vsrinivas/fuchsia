// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INDEX_NODE2_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INDEX_NODE2_H_

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace llvm {
class DWARFContext;
class DWARFDie;
}  // namespace llvm

namespace zxdb {

// An in-progress replacement for IndexNode. Not ready to use yet.
class IndexNode2 {
 public:
  using Map = std::map<std::string, IndexNode2>;

  enum class Kind {
    kNone = 0,
    kRoot,  // Root index node (meaning nothing semantically).
    kNamespace,
    kType,
    kFunction,
    kVar,
  };

  // A reference to a DIE that doesn't need the unit or the underlying llvm::DwarfDebugInfoEntry to
  // be kept. This allows the discarding of the full parsed DIE structures after indexing. It can be
  // converted back to a DIE, which will cause the DWARFUnit to be re-parsed.
  //
  // The offset stored in this structure is the offset from the beginning of the .debug_info
  // section, which is the same as the offset stored in the llvm::DWARFDebugInfoEntry.
  //
  // Random code reading the index can convert a DieRef to a Symbol object using
  // ModuleSymbols::IndexDieRefToSymbol().
  class DieRef {
   public:
    DieRef() = default;
    DieRef(bool is_decl, uint32_t offset) : is_declaration_(is_decl), offset_(offset) {}

    bool is_declaration() const { return is_declaration_; }
    uint32_t offset() const { return offset_; }

    // For use by ModuleSymbols. Other callers read the DieRef comments above.
    llvm::DWARFDie ToDie(llvm::DWARFContext* context) const;

   private:
    bool is_declaration_ = false;
    uint32_t offset_ = 0;
  };

  explicit IndexNode2(Kind kind) : kind_(kind) {}
  ~IndexNode2() = default;

  IndexNode2* AddChild(Kind kind, const char* name, const DieRef& ref);
  void AddDie(const DieRef& ref);

  const Map& namespaces() const { return namespaces_; }
  Map& namespaces() { return namespaces_; }

  const Map& types() const { return types_; }
  Map& types() { return types_; }

  const Map& functions() const { return functions_; }
  Map& functions() { return functions_; }

  const Map& vars() const { return vars_; }
  Map& vars() { return vars_; }

  // AsString is useful only in small unit tests since even a small module can have many megabytes
  // of dump.
  std::string AsString(int indent_level = 0) const;

  // Dump DIEs for debugging. A node does not contain its own name (this is stored in the parent's
  // map). If printing some node other than the root, specify the name.
  void Dump(std::ostream& out, int indent_level = 0) const;
  void Dump(const std::string& name, std::ostream& out, int indent_level = 0) const;

  // Merges another node's children into this one. It's assumed there are no duplicate DIEs so the
  // lists are just appended.
  void Merge(IndexNode2&& other);

 private:
  Kind kind_;

  // TODO(brettw) evaluate whether we can save a lot of memory using optionally-null unique_ptrs
  // here since in most cases all but on of these maps will be empty.
  Map namespaces_;
  Map types_;
  Map functions_;
  Map vars_;

  // Contains the references to the definitions (if possible) or the declarations (if not) of the
  // type, function, or variable. This will not have any entries for namespaces.
  //
  // TODO(brettw) consider an optimization because in most cases there will be exactly one DIE.
  std::vector<DieRef> dies_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INDEX_NODE2_H_
