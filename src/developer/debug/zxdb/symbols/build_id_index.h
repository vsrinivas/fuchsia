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
#include "src/developer/debug/zxdb/common/cache_dir.h"

namespace zxdb {

// This class provides symbol files from disk or remote servers.
//
// It can get files from different sources:
// 1. "ids.txt", which contains the mapping from Build IDs to ELF files.
// 2. ".build-id" directory, where an ELF file with Build ID "xxyyyy" is arranged at xx/yyyy.debug.
// 3. use a "symbol-index" file to get a list of "ids.txt" or ".build-id" directories.
// 4. explicitly given elf file path, or a directory of ELF files.
// 5. A symbol server, e.g., "gs://fuchsia-artifacts/debug".
class BuildIDIndex {
 public:
  struct Entry {
    // Empty string indicates no such file is found.

    // Debug info and program bits may be in separated files, e.g. .build-id/xx/xxxxxx.debug and
    // .build-id/xx/xxxxxx. The binary file could be optional because the debug_info file usually
    // also contains program bits.
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

  // GNU-style ".build-id" directories. The second string is the optional build directory.
  struct BuildIdDir {
    std::string path;
    std::string build_dir;
  };

  // "ids.txt" is a text file describing the mapping from the Build ID to the ELF file.
  struct IdsTxt {
    std::string path;
    std::string build_dir;
  };

  // Symbol servers.
  struct SymbolServer {
    std::string url;
    bool require_authentication;
  };

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

  // Adds a symbol server.
  void AddSymbolServer(const std::string& url, bool require_authentication = true);

  // cache_dir saves the downloaded symbol files. Its layout is the same as a build_id_dir but it
  // also features garbage collection.
  void SetCacheDir(const std::string& cache_dir);

  // Returns the path to the cache directory or an empty path if it's not set.
  std::filesystem::path GetCacheDir() const { return cache_dir_ ? cache_dir_->path() : ""; }

  // Add a symbol-index file that indexes various symbol sources.
  //
  // Two versions of symbol-index files are supported currently:
  //   - A plain text file separated by newlines and tabs, usually located at
  //     ~/.fuchsia/debug/symbol-index.
  //   - A rich JSON format that supports includes, globbing, usually located at
  //     ~/.fuchsia/debug/symbol-index.json.
  void AddSymbolIndexFile(const std::string& path);

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

  // Parses a build ID mapping file (ids.txt). This is separated and public only for testing
  // purposes. The results are added to the output. Returns the number of items loaded.
  static int ParseIDs(const std::string& input, const std::filesystem::path& containing_dir,
                      const std::string& build_dir, BuildIDMap* output);

  // Getters, mainly used in tests.
  const BuildIDMap& build_id_to_files() const { return build_id_to_files_; }
  const std::vector<BuildIdDir>& build_id_dirs() const { return build_id_dirs_; }
  const std::vector<IdsTxt>& ids_txts() const { return ids_txts_; }
  const std::vector<SymbolServer>& symbol_servers() const { return symbol_servers_; }

 private:
  // Updates the build_id_to_files_ cache if necessary.
  void EnsureCacheClean();

  // Logs an informational message.
  void LogMessage(const std::string& msg) const;

  // Adds all the mappings from the given ids.txt to the index.
  void LoadIdsTxt(const IdsTxt& ids_txt);

  // Populates build_id_dirs_, ids_txts_ and symbol_servers_ with the content of symbol-index file.
  void LoadSymbolIndexFile(const std::string& file_name);
  void LoadSymbolIndexFilePlain(const std::string& file_name);
  void LoadSymbolIndexFileJSON(const std::string& file_name);

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

  std::vector<BuildIdDir> build_id_dirs_;
  std::vector<IdsTxt> ids_txts_;
  std::vector<SymbolServer> symbol_servers_;

  // Cache directory. nullptr means no cache directory.
  std::unique_ptr<CacheDir> cache_dir_;

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
