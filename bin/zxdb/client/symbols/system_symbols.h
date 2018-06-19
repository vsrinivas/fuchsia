// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class ModuleSymbols;

// Tracks a global view of all ModuleSymbols objects. Since each object is
// independent of load address, we can share these between processes that
// load the same binary.
//
// This is an internal object but since there is no public API, there is no
// "Impl" split.
class SystemSymbols {
 public:
  // A reference-counted holder for the ModuleSymbols object. This object
  // will notify the owning SystemSymbols object when all references have
  // been destroyed.
  class ModuleRef : public fxl::RefCountedThreadSafe<ModuleRef> {
   public:
    ModuleRef(SystemSymbols* system_symbols,
              std::unique_ptr<ModuleSymbols> module_symbols);

    ModuleSymbols* module_symbols() { return module_symbols_.get(); }
    const ModuleSymbols* module_symbols() const {
      return module_symbols_.get();
    }

    // Notification from SystemSymbols that it's being deleted and no callbacks
    // should be issued on the pointer.
    void SystemSymbolsDeleting();

   private:
    FRIEND_REF_COUNTED_THREAD_SAFE(ModuleRef);
    ~ModuleRef();

    // May be null to indicate teh SystemSymbols object is deleted.
    SystemSymbols* system_symbols_;

    std::unique_ptr<ModuleSymbols> module_symbols_;
  };

  SystemSymbols();
  ~SystemSymbols();

  // Returns the directory to which paths are relative.
  const std::string& build_dir() const { return build_dir_; }

  // Loads the build ID file, clearing existing state. Returns true if the
  // load succeeded. In both success and failure case, *msg will be filled with
  // an informational message. See also AddBuildIDToFileMapping().
  bool LoadBuildIDFile(std::string* msg);

  // Explicitly inserts an ID mapping. Used for unit tests.
  void AddBuildIDToFileMapping(const std::string& build_id,
                               const std::string& file);

  // Injects a ModuleSymbols object for the given build ID. Used for testing.
  // Normally the test would provide a dummy implementation for ModuleSymbols.
  // Ownership of the symbols will be transferred to the returned refcounted
  // ModuleRef. As long as this is alive, the build id -> module mapping will
  // remain in the SystemSymbols object.
  fxl::RefPtr<ModuleRef> InjectModuleForTesting(
      const std::string& build_id, std::unique_ptr<ModuleSymbols> module);

  // Retrieves the symbols for the module with the given build ID. If the
  // module's symbols have already been loaded, just puts an owning reference
  // into the given out param. If not, the symbols will be loaded.
  //
  // This function uses the build_id for loading symbols. The name is only
  // used for generating informational messages.
  Err GetModule(const std::string& name_for_msg, const std::string& build_id,
                fxl::RefPtr<ModuleRef>* module);

  // Parses the BuildID-to-path mapping file contents. Returns a map from
  // build ID to local file.
  static std::map<std::string, std::string> ParseIds(const std::string& input);

 private:
  friend ModuleRef;

  // Notification from the ModuleRef that all references have been deleted and
  // the tracking information should be removed from the map.
  void WillDeleteModule(ModuleRef* module);

  // The directory to which paths are relative.
  std::string build_dir_;

  // Generated from the ids.txt file, this maps a build ID to a local file.
  std::map<std::string, std::string> build_id_to_file_;

  // Index from module build ID to a non-owning ModuleRef pointer. The
  // ModuleRef will notify us when it's being deleted so the pointers stay
  // up-to-date.
  std::map<std::string, ModuleRef*> modules_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SystemSymbols);
};

}  // namespace zxdb
