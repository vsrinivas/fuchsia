// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/focus.h"

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/fidl/json_xdr.h"

namespace modular {

FocusHandler::FocusHandler() {}
FocusHandler::~FocusHandler() = default;

void FocusHandler::AddProviderBinding(
    fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void FocusHandler::AddControllerBinding(
    fidl::InterfaceRequest<fuchsia::modular::FocusController> request) {
  controller_bindings_.AddBinding(this, std::move(request));
}

// |fuchsia::modular::FocusProvider|
void FocusHandler::Query(QueryCallback callback) {
  auto data = CurrentData();
  std::vector<fuchsia::modular::FocusInfo> infos {*data};
  callback(std::move(infos));
}

// |fuchsia::modular::FocusProvider|
void FocusHandler::Watch(fidl::InterfaceHandle<fuchsia::modular::FocusWatcher> watcher) {
  change_watchers_.push_back(watcher.Bind());
}

// |fuchsia::modular::FocusProvider|
void FocusHandler::Request(fidl::StringPtr story_id) {
  if (story_id.has_value()) {
    for (const auto& watcher : request_watchers_) {
      watcher->OnFocusRequest(story_id.value());
    }
  }
}

// |fuchsia::modular::FocusController|
void FocusHandler::Set(fidl::StringPtr story_id) {
  focused_story_id_ = std::move(story_id);
  last_focus_change_timestamp_ = time(nullptr);
  fuchsia::modular::FocusInfoPtr data = CurrentData();

  for (const auto& watcher : change_watchers_) {
    watcher->OnFocusChange(CloneOptional(data));
  }
}

// |fuchsia::modular::FocusController|
void FocusHandler::WatchRequest(
    fidl::InterfaceHandle<fuchsia::modular::FocusRequestWatcher> watcher) {
  request_watchers_.push_back(watcher.Bind());
}

fuchsia::modular::FocusInfoPtr FocusHandler::CurrentData() {
  fuchsia::modular::FocusInfoPtr data = fuchsia::modular::FocusInfo::New();
  data->focused_story_id = focused_story_id_.value_or("");
  data->last_focus_change_timestamp = last_focus_change_timestamp_;
  return data;
}

}  // namespace modular
