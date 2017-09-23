// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_STORY_STORAGE_IMPL_H_
#define PERIDOT_BIN_STORY_RUNNER_STORY_STORAGE_IMPL_H_

#include <string>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/per_device_story_info.fidl.h"
#include "lib/story/fidl/story_data.fidl.h"
#include "lib/surface/fidl/surface.fidl.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

class LinkImpl;
class XdrContext;

// A wrapper around a ledger page to store data related to a story that runs
// asynchronous operations pertaining to one Story instance in a dedicated
// OperationQueue instance.
class StoryStorageImpl : PageClient {
 public:
  using AllModuleDataCallback = std::function<void(fidl::Array<ModuleDataPtr>)>;
  using ModuleDataCallback = std::function<void(ModuleDataPtr)>;
  using DeviceDataCallback =
      std::function<void(PerDeviceStoryInfoPtr per_device)>;
  using LogCallback = std::function<void(fidl::Array<StoryContextLogPtr>)>;
  using SyncCallback = std::function<void()>;

  explicit StoryStorageImpl(LedgerClient* ledger_client, LedgerPageId page_id);
  ~StoryStorageImpl() override;

  void ReadModuleData(const fidl::Array<fidl::String>& module_path,
                      const ModuleDataCallback& callback);
  void ReadAllModuleData(const AllModuleDataCallback& callback);

  void WriteModuleData(const fidl::Array<fidl::String>& module_path,
                       const fidl::String& module_url,
                       const LinkPathPtr& link_path,
                       ModuleSource module_source,
                       const SurfaceRelationPtr& surface_relation,
                       bool module_stopped,
                       const SyncCallback& callback);
  void WriteModuleData(ModuleDataPtr data, const SyncCallback& callback);

  void WriteDeviceData(const std::string& story_id,
                       const std::string& device_id,
                       StoryState state,
                       const SyncCallback& callback);

  void Log(StoryContextLogPtr log_entry);
  void ReadLog(const LogCallback& callback);

  void Sync(const SyncCallback& callback);

 private:
  // All asynchronous operations are sequenced by this operation
  // queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStorageImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_STORY_STORAGE_IMPL_H_
