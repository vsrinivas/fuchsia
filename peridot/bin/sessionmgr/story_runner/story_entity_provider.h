// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_ENTITY_PROVIDER_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_ENTITY_PROVIDER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include <map>

#include "peridot/bin/sessionmgr/storage/story_storage.h"

namespace modular {

// The entity provider which is used to provide entities associated with a
// particular story.
class StoryEntityProvider : public fuchsia::modular::EntityProvider {
 public:
  // Creates a new StoryEntityProvider associated with the story backed by
  // |story_storage|.
  StoryEntityProvider(StoryStorage* story_storage);

  ~StoryEntityProvider();

  // Creates a new entity with the given |type| and |data|.
  //
  // |callback| will be called with the entity cookie, or an empty string if the
  //  creation failed.
  void CreateEntity(const std::string& type, fuchsia::mem::Buffer data,
                    fit::function<void(std::string /* cookie */)> callback);

  void Connect(fidl::InterfaceRequest<fuchsia::modular::EntityProvider> provider_request);

 private:
  // |fuchsia::modular::EntityProvider|
  void GetTypes(std::string cookie, GetTypesCallback callback) override;

  // |fuchsia::modular::EntityProvider|
  void GetData(std::string cookie, std::string type, GetDataCallback callback) override;

  // |fuchsia::modular::EntityProvider|
  void WriteData(std::string cookie, std::string type, fuchsia::mem::Buffer data,
                 WriteDataCallback callback) override;

  // |fuchsia::modular::EntityProvider|
  void Watch(std::string cookie, std::string type,
             fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) override;

  const std::string story_id_;
  StoryStorage* story_storage_;  // Not owned.

  fidl::BindingSet<fuchsia::modular::EntityProvider> provider_bindings_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_ENTITY_PROVIDER_H_
