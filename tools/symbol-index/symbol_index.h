// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOL_INDEX_SYMBOL_INDEX_H_
#define TOOLS_SYMBOL_INDEX_SYMBOL_INDEX_H_

#include <string>
#include <vector>

#include "tools/symbol-index/error.h"

namespace symbol_index {

class SymbolIndex {
 public:
  struct Entry {
    std::string symbol_path;
    std::string build_dir;

    Entry(const std::string& symbol_path, const std::string& build_dir)
        : symbol_path(symbol_path), build_dir(build_dir) {}

    std::string ToString() const;
  };

  // Constructs the symbol index from a config file at the given path. If the path is empty, a
  // default one, i.e., ~/.fuchsia/debug/symbol-index, will be used.
  explicit SymbolIndex(const std::string& path = "");

  // Loads the file from disk.  Does nothing if the file does not exists.
  // If any exception happens, the return value will be a non-empty string describing the error.
  Error Load();

  const std::vector<Entry>& entries() { return entries_; }

  // Adds a new symbol_path to the symbol index. The build_dir is optional.
  // Does nothing if the symbol_path is already in the symbol index, regardless of the build_dir.
  // Returns a bool indicating whether the insertion is actually done.
  bool Add(std::string symbol_path, std::string build_dir = "");

  // Reads the input and adds all symbol paths with optional build directories.
  // The input file could contain empty lines and comments. Paths in the empty line could also
  // be relative and will be canonicalized based on the input file.
  //
  // If the input_file is empty, stdin will be used and relative paths will be resolved based on
  // the current directory.
  Error AddAll(const std::string& input_file = "");

  // Removes the given symbol_path from the symbol index.
  // Does nothing if the symbol_path is not in the symbol index.
  // Returns a bool indicating whether the removal is actually done.
  bool Remove(std::string symbol_path);

  // Removes all non-existent paths from symbol-index.  Returns the removed entries.
  std::vector<Entry> Purge();

  // Saves the change to the file.
  // If any exception happens, the return value will be a non-empty string describing the error.
  Error Save();

 private:
  std::vector<Entry> entries_;
  std::string file_path_;
};

}  // namespace symbol_index

#endif  // TOOLS_SYMBOL_INDEX_SYMBOL_INDEX_H_
