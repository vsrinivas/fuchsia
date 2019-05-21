// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/story_entity_provider.h"

#include <utility>
#include "src/lib/uuid/uuid.h"

namespace modular {

StoryEntityProvider::StoryEntityProvider(StoryStorage* story_storage)
    : story_storage_(story_storage) {}

StoryEntityProvider::~StoryEntityProvider() = default;

void StoryEntityProvider::CreateEntity(
    const std::string& type, fuchsia::mem::Buffer data,
    fit::function<void(std::string /* cookie */)> callback) {
  const std::string cookie = uuid::Generate();
  story_storage_->SetEntityData(cookie, type, std::move(data))
      ->Then([cookie,
              callback = std::move(callback)](StoryStorage::Status status) {
        if (status == StoryStorage::Status::OK) {
          callback(cookie);
        } else {
          callback(nullptr);
        }
      });
}

void StoryEntityProvider::Connect(
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider> provider_request) {
  provider_bindings_.AddBinding(this, std::move(provider_request));
}

void StoryEntityProvider::GetTypes(std::string cookie,
                                   GetTypesCallback callback) {
  story_storage_->GetEntityType(cookie)->Then(
      [callback = std::move(callback)](StoryStorage::Status status,
                                       std::string type) {
        std::vector<std::string> types;
        types.push_back(type);
        callback(std::move(types));
      });
}

void StoryEntityProvider::GetData(std::string cookie, std::string type,
                                  GetDataCallback callback) {
  story_storage_->GetEntityData(cookie, type)
      ->Then([callback = std::move(callback)](StoryStorage::Status status,
                                              fuchsia::mem::BufferPtr data) {
        callback(std::move(data));
      });
}

void StoryEntityProvider::WriteData(std::string cookie, std::string type,
                                    fuchsia::mem::Buffer data,
                                    WriteDataCallback callback) {
  story_storage_->SetEntityData(cookie, type, std::move(data))
      ->Then([callback = std::move(callback)](StoryStorage::Status status) {
        switch (status) {
          case StoryStorage::Status::OK:
            callback(fuchsia::modular::EntityWriteStatus::OK);
            break;
          case StoryStorage::Status::INVALID_ENTITY_TYPE:
            // fallthrough
          case StoryStorage::Status::INVALID_ENTITY_COOKIE:
            // fallthrough
          case StoryStorage::Status::LEDGER_ERROR:
            // fallthrough
          case StoryStorage::Status::VMO_COPY_ERROR:
            callback(fuchsia::modular::EntityWriteStatus::ERROR);
            break;
        };
      });
}

void StoryEntityProvider::Watch(
    std::string cookie, std::string type,
    fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) {
  fuchsia::modular::EntityWatcherPtr entity_watcher = watcher.Bind();
  story_storage_->WatchEntity(cookie, type, std::move(entity_watcher));
}

}  // namespace modular
