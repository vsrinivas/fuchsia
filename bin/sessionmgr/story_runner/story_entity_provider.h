// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_ENTITY_PROVIDER_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_ENTITY_PROVIDER_H_

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

class StoryStorage;

// The entity provider which is used to provide entities associated with a
// particular story.
class StoryEntityProvider : public fuchsia::modular::EntityProvider {
 public:
  // Creates a new StoryEntityProvider associated with the story backed by
  // |story_storage|.
  StoryEntityProvider(StoryStorage* story_storage);

 private:
  // |fuchsia::modular::EntityProvider|
  void GetTypes(fidl::StringPtr cookie, GetTypesCallback callback) override;
  // |fuchsia::modular::EntityProvider|
  void GetData(fidl::StringPtr cookie, fidl::StringPtr type,
               GetDataCallback callback) override;
  // |fuchsia::modular::EntityProvider|
  void WriteData(fidl::StringPtr cookie, fidl::StringPtr type,
                 fuchsia::mem::Buffer data,
                 WriteDataCallback callback) override;
  // |fuchsia::modular::EntityProvider|
  void Watch(
      fidl::StringPtr cookie, fidl::StringPtr type,
      fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) override;

  StoryStorage* story_storage_;
}

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_ENTITY_PROVIDER_H_
