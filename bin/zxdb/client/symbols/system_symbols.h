// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>

#include "garnet/public/lib/fxl/macros.h"

namespace llvm {
namespace symbolize {
class LLVMSymbolizer;
}
}

namespace zxdb {

class SystemSymbols {
 public:
  SystemSymbols();
  ~SystemSymbols();

  const std::map<std::string, std::string>& build_id_to_file() const {
    return build_id_to_file_;
  }

  // The return value indicates whether the build ID file was loaded and
  // parsed properly. The object will be usable either way.
  //
  // The argument will be filled with a string describing the symbol load
  // state (or failure or it).
  bool Init(std::string* status_string);

  // Loads the build ID file, clearing existing state. Returns true if the file
  // was found and could be loaded.
  bool LoadBuildIDFile(const std::string& file_name);

  // Returns the path to a local file for the given build ID. Returns the empty
  // string if not found.
  std::string BuildIDToPath(const std::string& build_id) const;

  // Parses the BuildID-to-path mapping file contents. Returns a map from
  // build ID to local file.
  static std::map<std::string, std::string> ParseIds(const std::string& input);

  llvm::symbolize::LLVMSymbolizer* symbolizer() const {
    return symbolizer_.get();
  }

 private:
  std::map<std::string, std::string> build_id_to_file_;

  std::unique_ptr<llvm::symbolize::LLVMSymbolizer> symbolizer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SystemSymbols);
};

}  // namespace zxdb
