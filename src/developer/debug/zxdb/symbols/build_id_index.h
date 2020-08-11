// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_BUILD_ID_INDEX_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_BUILD_ID_INDEX_H_

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lib/fit/function.h"

namespace zxdb {

// This class maintains an index of build ID to local file path for files that may have symbols in
// them.
//
// It can get files from different sources:
// 1. "ids.txt", which contains the mapping from Build IDs to ELF files.
// 2. ".build-id" directory, where an ELF file with Build ID "xxyyyy" is arranged at xx/yyyy.debug.
// 3. use a "symbol-index" file to get a list of "ids.txt" or ".build-id" directories.
// 4. explicitly given elf file path, or a directory of ELF files.
class BuildIDIndex {
 public:
  struct Entry {
    // Debug info and program bits may be in separated files, e.g. .build-id/xx/xxxxxx.debug and
    // .build-id/xx/xxxxxx.
    std::string debug_info;
    std::string binary;
    // The build directory is useful when looking up source code in e.g. "list" command.
    // It's typically only available when the file is provided by a symbol-index file and that file
    // contains build directory information.
    std::string build_dir;
  };
  using BuildIDMap = std::map<std::string, Entry>;

  // Lists symbol sources and the number of ELF files indexed at that location.
  using StatusList = std::vector<std::pair<std::string, int>>;

  static constexpr int kStatusIsFolder = -1;

  BuildIDIndex() = default;
  ~BuildIDIndex() = default;

  // Sets the callback for informational messages. Null callbacks are legal.
  void set_information_callback(fit::function<void(const std::string&)> fn) {
    information_callback_ = std::move(fn);
  }

  // Return the entry associated with the given build_id. This is the designated way to obtain
  // information from a BuildIDIndex.
  //
  // The return value could include empty strings for missing values. If the build_id is not found
  // anywhere, the entry will include 3 empty strings for debug_info, binary and build_dir.
  //
  // This function also caches the result for symbol files found in .build-id directory. Thus any
  // subsequent calls will just get the same cached entry, even if the symbol files are created on
  // the filesystem later. In this case, AddOneFile can be used to force indexing a file.
  Entry EntryForBuildID(const std::string& build_id);

  // Clears all symbol sources. No symbols can be loaded after this call until Add*() is called.
  void ClearAll();

  // Manually inserts a mapping of a build ID to a file name. The file is probed for its build ID
  // and type, and if not found or not a valid ELF file, it is ignored and we return false.
  // The added mapping will remain across cache clears.
  bool AddOneFile(const std::string& file_name);

  // Manually inserts a mapping of a build ID to a file name with the given type.
  void AddBuildIDMappingForTest(const std::string& build_id, const std::string& file_name);

  // Adds an "ids.txt" file that maps build ID to file paths. Will verify that the path is already
  // there and ignore it if so. An optional build_dir could be supplemented to help look up the
  // source code.
  void AddIdsTxt(const std::string& ids_txt, const std::string& build_dir = "");

  // Adds a GNU-style symbol repository to the search index. The path given should contain files of
  // the form ab/cdefg.debug, where abcdefg is the build ID. An optional build_dir could be
  // supplemented to help look up the source code.
  void AddBuildIdDir(const std::string& dir, const std::string& build_dir = "");

  // Populates build_id_dirs_ and ids_txts_ with the content of symbol-index file.
  void AddSymbolIndexFile(const std::string& symbol_index);

  // Adds a file or directory to the symbol search index. If the path is a file this class will try
  // to parse it as an ELF file and add it to the index if it is. If the path is a directory, all
  // files in that directory will be indexed.
  //
  // Will ignore the path if it's already loaded.
  void AddPlainFileOrDir(const std::string& path);

  // Returns the status of the symbols. This will force the cache to be fresh
  // so may cause I/O.
  StatusList GetStatus();

  // Clears all cached build IDs. They will be reloaded when required.
  void ClearCache();

  // Parses a build ID mapping file (ids.txt). This is a separate static function for testing
  // purposes. The results are added to the output. Returns the number of items loaded.
  static int ParseIDs(const std::string& input, const std::filesystem::path& containing_dir,
                      const std::string& build_dir, BuildIDMap* output);

  // Returns the underlying mapping.
  const BuildIDMap& build_id_to_files() const { return build_id_to_files_; }

 private:
  // Updates the build_id_to_files_ cache if necessary.
  void EnsureCacheClean();

  // Logs an informational message.
  void LogMessage(const std::string& msg) const;

  // Adds all the mappings from the given ids.txt to the index.
  void LoadIdsTxt(const std::string& file_name, const std::string& build_dir);

  // Adds "ids.txt" / ".build-id" directories in the given symbol-index file to the index.
  void LoadSymbolIndexFile(const std::string& file_name);

  // Adds all the mappings from the given file or directory to the index.
  void IndexSourcePath(const std::string& path);

  // Indexes one ELF file and adds it to the index. Returns true if it was an ELF file and it was
  // added to the index. If preserve is set to true, the indexing result will be cached in
  // manual_mappings_, so it will remain across cache clears.
  //
  // The function does nothing if the same build ID already exists in the build_id_to_files_
  // mapping, so that the order of ids_txts_ / build_id_dirs_ matters.
  bool IndexSourceFile(const std::string& file_path, const std::string& build_dir = "",
                       bool preserve = false);

  // Search the .build-id directories for the given build ID.
  void SearchBuildIdDirs(const std::string& build_id);

  // Function to output informational messages. May be null. Use LogMessage().
  fit::function<void(const std::string&)> information_callback_;

  // GNU-style ".build-id" directories. The second string is the optional build directory.
  std::vector<std::pair<std::string, std::string>> build_id_dirs_;

  // "ids.txt" is a text file describing the mapping from the Build ID to the ELF file.
  std::vector<std::pair<std::string, std::string>> ids_txts_;

  // "symbol-index" files are used to populate the build_id_dirs_ and ids_txts_ above.
  std::vector<std::string> symbol_index_files_;

  // Plain ELF files or directories of ELF files to index.
  std::vector<std::string> sources_;

  // Maintains the logs of how many symbols were indexed for each location.
  StatusList status_;

  // Indicates if build_id_to_files_ is up-to-date. This is necessary to disambiguate whether an
  // empty cache means "not scanned" or "nothing found".
  bool cache_dirty_ = true;

  // Manually-added build ID mappings. This is not cleared when the cache is cleared, and these are
  // added to the mappings when the cache is rebuilt.
  BuildIDMap manual_mappings_;

  // Index of build IDs to local file paths.
  //
  // Note: at the beginning, build_id_to_files only stores the mapping from ids.txt or plain ELF
  // files that needs to be indexed ahead of time. Files in .build-id directories are added to this
  // mapping only when they are required by EntryForBuildID.
  BuildIDMap build_id_to_files_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_BUILD_ID_INDEX_H_
