// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/story_entity_provider.h"

namespace modular {

StoryEntityProvider::StoryEntityProvider(StoryStorage* story_storage)
    : story_storage_(story_storage) {}

void StoryEntityProvider::GetTypes(fidl::StringPtr cookie,
                                   GetTypesCallback callback) {}

void StoryEntityProvider::GetData(fidl::StringPtr cookie, fidl::StringPtr type,
                                  GetDataCallback callback) {}

void StoryEntityProvider::WriteData(fidl::StringPtr cookie,
                                    fidl::StringPtr type,
                                    fuchsia::mem::Buffer data,
                                    WriteDataCallback callback) {}

void StoryEntityProvider::Watch(
    fidl::StringPtr cookie, fidl::StringPtr type,
    fidl::InterfaceHandle<fuchsia::modular::EntityWatcher> watcher) {}

}  // namespace modular
