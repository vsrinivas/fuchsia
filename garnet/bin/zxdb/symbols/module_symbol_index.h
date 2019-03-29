// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "garnet/bin/zxdb/symbols/module_symbol_index_node.h"
#include "src/lib/fxl/macros.h"

namespace llvm {

class DWARFCompileUnit;
class DWARFContext;
class DWARFDie;
class DWARFUnit;

namespace object {
class ObjectFile;
}  // namespace object

}  // namespace llvm

namespace zxdb {

// Holds the index of symbols for a given module.
class ModuleSymbolIndex {
 public:
  ModuleSymbolIndex();
  ~ModuleSymbolIndex();

  // This function takes an object file rather than a context so it can create
  // its own context, and then discard the context when it's done. Since most
  // debugging information is not needed after indexing, this saves a lot of
  // memory.
  void CreateIndex(llvm::object::ObjectFile* object_file);

  const ModuleSymbolIndexNode& root() const { return root_; }
  ModuleSymbolIndexNode& root() { return root_; }

  size_t files_indexed() const { return file_name_index_.size(); }

  // Returns how many symbols are indexed. This iterates through everything so
  // can be slow.
  size_t CountSymbolsIndexed() const;

  // Takes a fully-qualified name with namespaces and classes and template
  // parameters and returns the list of symbols which match exactly.
  //
  // TODO(brettw) remove the version that takes a bare string. It doesn't know
  // how to split things properly with templates.
  //
  // The vector version takes a list of components that have been split on
  // "::". Most callers should use Identifier::GetAsIndexComponents() to get
  // this list in the correct format.
  const std::vector<ModuleSymbolIndexNode::DieRef>& FindExact(
      const std::string& input) const;
  const std::vector<ModuleSymbolIndexNode::DieRef>& FindExact(
      const std::vector<std::string>& input) const;

  // Takes a fully-qualified name with namespaces and classes and returns a
  // pair of iterators.
  //
  // The first iterator points to the first node that has the input as a
  // prefix.
  //
  // The second returned iterator points to the last node IN THE CONTAINER.
  // This does not indicate the last node with the prefix. Many callers won't
  // need all of the matches and doing it this way avoids a second lookup.
  //
  // Non-last input nodes must match exactly with "std::string::operator==".
  // For example, the input:
  //   { "std", "vector<" }
  // Would look in the "std" node and would return an iterator to the
  // "vector<Aardvark>" node inside it and the end of the "std" mode. Nodes are
  // sorted by "std::string::operator<".
  //
  // IF there are no matches both iterators will be the same (found == end).
  //
  // If the caller wants to find all matching prefixes, it can advance the
  // iterator as long as the last input component is a prefix if the current
  // iterator key and less than the end.
  std::pair<ModuleSymbolIndexNode::ConstIterator,
            ModuleSymbolIndexNode::ConstIterator>
  FindPrefix(const std::vector<std::string>& input) const;

  // Looks up the name in the file index and returns the set of matches. The
  // name is matched from the right side with a left boundary of either a slash
  // or the beginning of the full path. This may match more than one file name,
  // and the caller is left to decide which one(s) it wants.
  std::vector<std::string> FindFileMatches(const std::string& name) const;

  // Looks up the given exact file path and returns all compile units it
  // appears in. The file must be an exact match (normally it's one of the
  // results from FindFileMatches).
  //
  // The contents of the vector are indices into the compilation unit array.
  // (see llvm::DWARFContext::getCompileUnitAtIndex).
  const std::vector<unsigned>* FindFileUnitIndices(
      const std::string& name) const;

  // Dumps the file index to the stream for debugging.
  void DumpFileIndex(std::ostream& out) const;

 private:
  void IndexCompileUnit(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                        unsigned unit_index);

  void IndexCompileUnitSourceFiles(llvm::DWARFContext* context,
                                   llvm::DWARFUnit* unit, unsigned unit_index);

  // Populates the file_name_index_ given a now-unchanging files_ map.
  void IndexFileNames();

  ModuleSymbolIndexNode root_;

  // Maps full path names to compile units that reference them. This must not
  // be mutated once the file_name_index_ is built.
  //
  // The contents of the vector are indices into the compilation unit array.
  // (see llvm::DWARFContext::getCompileUnitAtIndex). These are "unsigned"
  // type because that's what LLVM uses for these indices.
  //
  // This is a map, not a multimap, because some files will appear in many
  // compilation units. I suspect it's better to avoid duplicating the names
  // (like a multimap would) and eating the cost of indirect heap allocations
  // for vectors in the single-item case.
  using FileIndex = std::map<std::string, std::vector<unsigned>>;
  FileIndex files_;

  // Maps the last file name component (the part following the last slash) to
  // the set of entries in the files_ index that have that name.
  //
  // This is a multimap because the name parts will generally be unique so we
  // should get few duplicates. The cost of using a vector for most items
  // containing one element becomes higher in that case.
  using FileNameIndex =
      std::multimap<std::string_view, FileIndex::const_iterator>;
  FileNameIndex file_name_index_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbolIndex);
};

}  // namespace zxdb
