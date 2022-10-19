// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_DIE_SCANNER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_DIE_SCANNER_H_

#include <lib/syslog/cpp/macros.h>
#include <stdint.h>

#include <utility>
#include <vector>

namespace llvm {

class DWARFDebugInfoEntry;
class DWARFUnit;

}  // namespace llvm

namespace zxdb {

// This is a helper class for generating a zxdb::Index.
//
// It works in two phases. In the first, it linearly iterates through the DIEs of a unit. The
// calling code does:
//
//    while (!scanner.done()) {
//      current_die = scanner.Prepare();
//      ... work on current_die ...
//      scanner.Advance();
//    }
//
// In the second phase, the scanner can provide extra information for a DIE in the unit in constant
// time.
//
// This class exists because in LLVM, getting the parent of a DIE used to require an inefficient
// linear search. Since LLVM provides a direct way to get parent index, the necessity of this class
// is largely elimiated.
class DwarfDieScanner {
 public:
  static constexpr uint32_t kNoParent = static_cast<uint32_t>(-1);

  // The unit pointer must outlive this class.
  explicit DwarfDieScanner(llvm::DWARFUnit* unit);

  ~DwarfDieScanner();

  // Call at the beginning of each iteration (when !done()) to get the current DIE. This is required
  // to be called before Advance() as it sets some internal state.
  const llvm::DWARFDebugInfoEntry* Prepare();

  // Advances to the next DIE.
  void Advance();

  uint32_t die_index() const { return die_index_; }
  uint32_t die_count() const { return die_count_; }

  bool done() const { return die_index_ == die_count_; }

  // Returns true if the current stack position is considered to be directly inside a function.
  // Lexical blocks count as being inside a function, but if a new type is defined inside a function
  // the children of that type are no longer considered to be inside a function.
  //
  // This is used to avoid indexing function-local variables.
  bool is_inside_function() const { return tree_stack_.back().inside_function; }

  // Return the parent's index of a DIE in constant time. Will return kNoParent for the root.
  uint32_t GetParentIndex(uint32_t index) const;

 private:
  // Stores the list of parent indices according to the current depth in the tree. At any given
  // point, the parent index of the current node will be tree_stack.back(). inside_function should
  // be set if this node or any parent node is a function.
  struct StackEntry {
    StackEntry(unsigned i, bool f) : index(i), inside_function(f) {}

    unsigned index;

    // Tracks whether this node is a child of a function with no intermediate types. This is to
    // avoid indexing local variables inside functions or inside blocks inside functions.
    bool inside_function;
  };

  llvm::DWARFUnit* unit_;

  uint32_t die_count_ = 0;
  uint32_t die_index_ = 0;

  const llvm::DWARFDebugInfoEntry* cur_die_ = nullptr;

  std::vector<StackEntry> tree_stack_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_DIE_SCANNER_H_
