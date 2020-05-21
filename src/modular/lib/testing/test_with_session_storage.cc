// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular_testing {

TestWithSessionStorage::TestWithSessionStorage() = default;
TestWithSessionStorage::~TestWithSessionStorage() = default;

std::unique_ptr<modular::SessionStorage> TestWithSessionStorage::MakeSessionStorage() {
  return std::make_unique<modular::SessionStorage>();
}

std::shared_ptr<modular::StoryStorage> TestWithSessionStorage::GetStoryStorage(
    modular::SessionStorage* const storage, std::string story_id) {
  auto story_storage = storage->GetStoryStorage(story_id);
  FX_DCHECK(!!story_storage) << story_id;
  return story_storage;
}

void TestWithSessionStorage::WriteModuleData(modular::StoryStorage* const story_storage,
                                             fuchsia::modular::ModuleData module_data) {
  story_storage->WriteModuleData(std::move(module_data));
}

}  // namespace modular_testing
