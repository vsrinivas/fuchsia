// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/story_storage.h"

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/modular/bin/sessionmgr/storage/encode_module_path.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

fuchsia::modular::ModuleData CloneModuleData(const fuchsia::modular::ModuleData& module_data) {
  fuchsia::modular::ModuleData copy;
  fidl::Clone(module_data, &copy);

  if (module_data.has_additional_services() &&
      module_data.additional_services().host_directory.is_valid()) {
    copy.mutable_additional_services()->host_directory.set_channel(zx::channel(
        fdio_service_clone(module_data.additional_services().host_directory.channel().get())));
  }

  return copy;
}

void StoryStorage::WriteModuleData(ModuleData module_data) {
  auto module_path = fidl::Clone(module_data.module_path());
  auto key = EncodeModulePath(module_path);
  auto saved = CloneModuleData(module_data);
  module_data_backing_storage_[key] = std::move(saved);

  module_data_updated_watchers_.Notify(module_data);
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
    module_data_updated_watchers_.Notify(it->second);
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
  for (auto& it : module_data_backing_storage_) {
    vec.push_back(CloneModuleData(it.second));
  }
  return vec;
}

}  // namespace modular
