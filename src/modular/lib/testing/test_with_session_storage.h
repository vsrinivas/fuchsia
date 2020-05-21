// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_
#define SRC_MODULAR_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_

#include <fuchsia/modular/cpp/fidl.h>

#include <lib/gtest/real_loop_fixture.h>

#include "src/modular/bin/sessionmgr/storage/session_storage.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"

namespace modular_testing {

class TestWithSessionStorage : public gtest::RealLoopFixture {
 public:
  TestWithSessionStorage();
  ~TestWithSessionStorage() override;

 protected:
  std::unique_ptr<modular::SessionStorage> MakeSessionStorage();

  std::shared_ptr<modular::StoryStorage> GetStoryStorage(modular::SessionStorage* storage,
                                                         std::string story_id);

  void WriteModuleData(modular::StoryStorage* story_storage,
                       fuchsia::modular::ModuleData module_data);

 private:
  // Implements CreateStory on behalf of protected variants
  fidl::StringPtr CreateStoryImpl(fidl::StringPtr story_id, modular::SessionStorage* storage);
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_TEST_WITH_SESSION_STORAGE_H_
