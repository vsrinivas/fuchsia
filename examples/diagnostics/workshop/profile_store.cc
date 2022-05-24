// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile_store.h"

#include <memory>

#include "file_utils.h"
#include "profile.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

ProfileStore::ProfileStore(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

ProfileStore::~ProfileStore() = default;

void ProfileStore::Open(std::string key,
                        fidl::InterfaceRequest<fuchsia::examples::diagnostics::Profile> channel) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    it->second->AddBinding(std::move(channel));
  } else {
    std::string filepath = FilepathForKey(key);
    // Only open if the profile has previously been created.
    if (files::IsFile(filepath)) {
      auto profile = std::make_unique<Profile>(dispatcher_, filepath);
      profile->AddBinding(std::move(channel));
      profiles_[std::move(key)] = std::move(profile);
    }
  }
}

void ProfileStore::CreateOrOpen(
    std::string key, fidl::InterfaceRequest<::fuchsia::examples::diagnostics::Profile> channel) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    it->second->AddBinding(std::move(channel));
  } else {
    std::string filepath = FilepathForKey(key);
    auto profile = std::make_unique<Profile>(dispatcher_, filepath);
    profile->AddBinding(std::move(channel));
    profiles_[std::move(key)] = std::move(profile);
  }
}

void ProfileStore::Delete(std::string key, DeleteCallback callback) {
  std::string filepath = FilepathForKey(key);
  if (files::IsFile(filepath)) {
    callback(files::DeletePath(filepath, false));
  }
  callback(false);
}

void ProfileStore::OpenReader(
    std::string key,
    fidl::InterfaceRequest<fuchsia::examples::diagnostics::ProfileReader> channel) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    it->second->AddReaderBinding(std::move(channel));
  } else {
    std::string filepath = FilepathForKey(key);
    // Only open if the profile has previously been created.
    if (files::IsFile(filepath)) {
      auto profile = std::make_unique<Profile>(dispatcher_, filepath);
      profile->AddReaderBinding(std::move(channel));
      profiles_[std::move(key)] = std::move(profile);
    }
  }
}
