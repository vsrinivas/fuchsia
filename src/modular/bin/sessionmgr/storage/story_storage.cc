// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/story_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/bin/sessionmgr/storage/encode_module_path.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

namespace {}  // namespace

StoryStorage::StoryStorage() {}

void StoryStorage::WriteModuleData(ModuleData module_data) {
  auto module_path = fidl::Clone(module_data.module_path());
  auto key = EncodeModulePath(module_path);
  auto saved = CloneOptional(module_data);
  module_data_backing_storage_[key] = std::move(*saved);

  DispatchWatchers(module_data);
}

bool StoryStorage::MarkModuleAsDeleted(const std::vector<std::string>& module_path) {
  auto key = EncodeModulePath(module_path);
  // Pull ModuleData out of map and clone its contents into data, if present.
  auto it = module_data_backing_storage_.find(key);
  if (it == module_data_backing_storage_.end()) {
    return false;
  }
  if (!it->second.has_module_deleted() || !it->second.module_deleted()) {
    it->second.set_module_deleted(true);
    DispatchWatchers(it->second);
  }
  return true;
}

ModuleDataPtr StoryStorage::ReadModuleData(const std::vector<std::string>& module_path) {
  auto key = EncodeModulePath(module_path);
  auto it = module_data_backing_storage_.find(key);
  ModuleDataPtr data{};
  if (it != module_data_backing_storage_.end()) {
    data = CloneOptional(it->second);
  }
  return data;
}

std::vector<ModuleData> StoryStorage::ReadAllModuleData() {
  std::vector<ModuleData> vec;
  vec.reserve(module_data_backing_storage_.size());
  for (auto it = module_data_backing_storage_.begin(); it != module_data_backing_storage_.end();
       ++it) {
    ModuleData elem;
    it->second.Clone(&elem);
    vec.push_back(std::move(elem));
  }
  return vec;
}

void StoryStorage::DispatchWatchers(ModuleData& module_data) {
  auto callbacks = std::move(module_data_updated_watchers_);
  module_data_updated_watchers_.clear();
  for (size_t i = 0; i < callbacks.size(); i++) {
    auto callback_data = CloneOptional(module_data);
    auto callback = std::move(callbacks[i]);
    auto notification_interest = callback(std::move(*callback_data));
    if (notification_interest == NotificationInterest::CONTINUE) {
      module_data_updated_watchers_.push_back(std::move(callback));
    }
  }
}

}  // namespace modular
