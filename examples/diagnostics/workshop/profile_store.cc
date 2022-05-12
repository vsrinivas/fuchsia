// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile_store.h"

#include <memory>

#include "profile.h"

ProfileStore::ProfileStore(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

ProfileStore::~ProfileStore() = default;

void ProfileStore::Open(std::string key,
                        fidl::InterfaceRequest<fuchsia::examples::diagnostics::Profile> channel) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    it->second->AddBinding(std::move(channel));
  }
}

void ProfileStore::CreateOrOpen(
    std::string key, fidl::InterfaceRequest<::fuchsia::examples::diagnostics::Profile> channel) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    it->second->AddBinding(std::move(channel));
  } else {
    auto profile = std::make_unique<Profile>(dispatcher_);
    profile->AddBinding(std::move(channel));
    profiles_[std::move(key)] = std::move(profile);
  }
}

void ProfileStore::Delete(std::string key, DeleteCallback callback) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    profiles_.erase(it);
    callback(true);
  } else {
    callback(false);
  }
}

void ProfileStore::OpenReader(
    std::string key,
    fidl::InterfaceRequest<fuchsia::examples::diagnostics::ProfileReader> channel) {
  auto it = profiles_.find(key);
  if (it != profiles_.end()) {
    it->second->AddReaderBinding(std::move(channel));
  }
}
