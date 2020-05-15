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
#include "src/lib/fxl/strings/string_view.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

namespace {}  // namespace

StoryStorage::StoryStorage() {}

void StoryStorage::WriteModuleData(ModuleData module_data) {
  auto module_path = fidl::Clone(module_data.module_path());
  auto key = MakeModuleKey(module_path);
  auto saved = CloneOptional(module_data);
  module_data_backing_storage_[key] = std::move(*saved);

  DispatchWatchers(module_data);
}

void StoryStorage::UpdateModuleData(const std::vector<std::string>& module_path,
                                    fit::function<void(ModuleDataPtr*)> mutate_fn) {
  auto key = MakeModuleKey(module_path);

  ModuleDataPtr data{};
  // Pull ModuleData out of map and clone its contents into data, if present.
  auto it = module_data_backing_storage_.find(key);
  if (it != module_data_backing_storage_.end()) {
    data = ModuleData::New();
    fidl::Clone(it->second, data.get());
  }

  // Call mutate_fn.
  mutate_fn(&data);

  // Write back to map.
  if (data) {
    bool changed = !fidl::Equals(*data, it->second);
    auto saved = CloneOptional(data);
    module_data_backing_storage_[key] = std::move(*saved);
    if (changed) {
      DispatchWatchers(*data);
    }
  } else {
    auto previously_populated = it != module_data_backing_storage_.end();
    FX_DCHECK(!previously_populated) << "StoryStorage::UpdateModuleData(): mutate_fn() must not "
                                        "set to null an existing ModuleData record.";
  }
}

ModuleDataPtr StoryStorage::ReadModuleData(const std::vector<std::string>& module_path) {
  auto key = MakeModuleKey(module_path);
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

namespace {

constexpr char kJsonNull[] = "null";

}  // namespace

FuturePtr<StoryStorage::Status, std::string> StoryStorage::GetLinkValue(const LinkPath& link_path) {
  auto key = MakeLinkKey(link_path);
  auto it = link_backing_storage_.find(key);
  std::string val = it != link_backing_storage_.end() ? it->second : kJsonNull;

  return Future<StoryStorage::Status, std::string>::CreateCompleted(
      "StoryStorage::GetLinkValue " + key, Status::OK, std::move(val));
}

FuturePtr<StoryStorage::Status> StoryStorage::UpdateLinkValue(
    const LinkPath& link_path, fit::function<void(fidl::StringPtr*)> mutate_fn,
    const void* context) {
  // nullptr is reserved for updates that came from other instances of
  // StoryStorage.
  FX_DCHECK(context != nullptr)
      << "StoryStorage::UpdateLinkValue(..., context) of nullptr is reserved.";

  auto key = MakeLinkKey(link_path);

  // Pull exisitng link value out of backing store, if present.
  fidl::StringPtr scratch;
  auto it = link_backing_storage_.find(key);
  if (it != link_backing_storage_.end()) {
    scratch = std::move(it->second);
  }

  // Let mutate_fn update the link value
  mutate_fn(&scratch);

  // Write back to the backing store.
  link_backing_storage_[key] = std::move(scratch.value());

  // Return completed future.
  return Future<StoryStorage::Status>::CreateCompleted("StoryStorage.UpdateLinkValue", Status::OK);
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
