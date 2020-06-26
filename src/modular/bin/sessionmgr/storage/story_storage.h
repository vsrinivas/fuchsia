// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_STORY_STORAGE_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_STORY_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fit/defer.h>

#include <map>

#include "src/modular/lib/async/cpp/future.h"

using fuchsia::modular::ModuleData;
using fuchsia::modular::ModuleDataPtr;

namespace modular {

// This class has the following responsibilities:
//
// * Manage the persistence of metadata about what mods are part of a single
//   story.
class StoryStorage {
 public:
  // Constructs a new StoryStorage with self-contained storage.
  StoryStorage();

  enum class Status {
    OK = 0,
    LEDGER_ERROR = 1,
    VMO_COPY_ERROR = 2,
    // Indicates the storage operation detected either an invalid or conflicting
    // entity type (e.g. an empty type string or a write with a mismatched
    // type).
    INVALID_ENTITY_TYPE = 3,
    // Indicates the storage operation detected an invalid entity cookie (e.g.
    // an empty cookie).
    INVALID_ENTITY_COOKIE = 4,
  };

  enum class NotificationInterest {
    // Indicates the returning function wishes to be removed from the set of
    // module data change watchers, should not be called again, and that
    // StoryStorage should release its reference to the function.
    STOP = 0,
    // Indicates the returning function wishes to continue receiving module data
    // change notifications.
    CONTINUE = 1,
  };

  // Adds a callback to be called whenever ModuleData is added or updated in the
  // underlying storage.  When the provided callback is triggered, the return
  // value is used to express whether the callback wishes to be unsubscribed
  // from future notifications or not.
  void SubscribeModuleDataUpdated(fit::function<NotificationInterest(ModuleData)> callback) {
    module_data_updated_watchers_.push_back(std::move(callback));
  }

  // =========================================================================
  // ModuleData

  // Returns the current ModuleData for |module_path|. If not found, the
  // returned value is null.
  ModuleDataPtr ReadModuleData(const std::vector<std::string>& module_path);

  // Writes |module_data| to storage. The returned future is completed
  // once |module_data| has been written and a notification confirming the
  // write has been received.
  void WriteModuleData(ModuleData module_data);

  // Marks the ModuleData.module_deleted field to 'true' for the module at
  // |module_path|. Returns false no module with |module_path| exists.
  bool MarkModuleAsDeleted(const std::vector<std::string>& module_path);

  // Returns all ModuleData entries for all mods.
  std::vector<ModuleData> ReadAllModuleData();

 private:
  void DispatchWatchers(ModuleData& module_data);

  // The actual module data, indexed by a key derived from module_data.module_path() values.
  std::map<std::string, ModuleData> module_data_backing_storage_;

  // List of watchers to call when ModuleData is created.
  // Each watcher will be called for each ModuleData change until it returns
  // true, indicating satisfaction, at which point StoryStorage will remove
  // the callback from the watch list.
  std::vector<fit::function<NotificationInterest(ModuleData)>> module_data_updated_watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStorage);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_STORY_STORAGE_H_
