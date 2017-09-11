// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/focus.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/operations.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fxl/time/time_point.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

// Serialization and deserialization of FocusInfo to and from JSON.
void XdrFocusInfo(XdrContext* const xdr, FocusInfo* const data) {
  xdr->Field("device_id", &data->device_id);
  xdr->Field("focused_story_id", &data->focused_story_id);
  xdr->Field("last_focus_timestamp", &data->last_focus_change_timestamp);
}

}  // namespace

FocusHandler::FocusHandler(const fidl::String& device_id,
                           LedgerClient* const ledger_client,
                           LedgerPageId page_id)
    : PageClient("FocusHandler", ledger_client, std::move(page_id), kFocusKeyPrefix),
      device_id_(device_id) {}

FocusHandler::~FocusHandler() = default;

void FocusHandler::AddProviderBinding(
    fidl::InterfaceRequest<FocusProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void FocusHandler::AddControllerBinding(
    fidl::InterfaceRequest<FocusController> request) {
  controller_bindings_.AddBinding(this, std::move(request));
}

// |FocusProvider|
void FocusHandler::Query(const QueryCallback& callback) {
  new ReadAllDataCall<FocusInfo, fidl::InlinedStructPtr<FocusInfo>>(
      &operation_queue_, page(), kFocusKeyPrefix, XdrFocusInfo, callback);
}

// |FocusProvider|
void FocusHandler::Watch(fidl::InterfaceHandle<FocusWatcher> watcher) {
  change_watchers_.push_back(FocusWatcherPtr::Create(std::move(watcher)));
}

// |FocusProvider|
void FocusHandler::Request(const fidl::String& story_id) {
  for (const auto& watcher : request_watchers_) {
    watcher->OnFocusRequest(story_id);
  }
}

// |FocusProvider|
void FocusHandler::Duplicate(fidl::InterfaceRequest<FocusProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

// |FocusController|
void FocusHandler::Set(const fidl::String& story_id) {
  FocusInfoPtr data = FocusInfo::New();
  data->device_id = device_id_;
  data->focused_story_id = story_id;
  data->last_focus_change_timestamp =
      fxl::TimePoint::Now().ToEpochDelta().ToSeconds();

  new WriteDataCall<FocusInfo, fidl::InlinedStructPtr<FocusInfo>>(
      &operation_queue_, page(), MakeFocusKey(device_id_), XdrFocusInfo,
      std::move(data), [] {});
}

// |FocusController|
void FocusHandler::WatchRequest(
    fidl::InterfaceHandle<FocusRequestWatcher> watcher) {
  request_watchers_.push_back(
      FocusRequestWatcherPtr::Create(std::move(watcher)));
}

// |PageClient|
void FocusHandler::OnPageChange(const std::string& /*key*/,
                                const std::string& value) {
  auto focus_info = FocusInfo::New();
  if (!XdrRead(value, &focus_info, XdrFocusInfo)) {
    return;
  }

  for (const auto& watcher : change_watchers_) {
    watcher->OnFocusChange(focus_info.Clone());
  }
}

VisibleStoriesHandler::VisibleStoriesHandler()
    : visible_stories_(fidl::Array<fidl::String>::New(0)) {}

VisibleStoriesHandler::~VisibleStoriesHandler() = default;

void VisibleStoriesHandler::AddProviderBinding(
    fidl::InterfaceRequest<VisibleStoriesProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void VisibleStoriesHandler::AddControllerBinding(
    fidl::InterfaceRequest<VisibleStoriesController> request) {
  controller_bindings_.AddBinding(this, std::move(request));
}

void VisibleStoriesHandler::Query(const QueryCallback& callback) {
  callback(visible_stories_.Clone());
}

void VisibleStoriesHandler::Watch(
    fidl::InterfaceHandle<VisibleStoriesWatcher> watcher) {
  change_watchers_.push_back(
      VisibleStoriesWatcherPtr::Create(std::move(watcher)));
}

void VisibleStoriesHandler::Duplicate(
    fidl::InterfaceRequest<VisibleStoriesProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void VisibleStoriesHandler::Set(fidl::Array<fidl::String> story_ids) {
  visible_stories_ = std::move(story_ids);
  for (const auto& watcher : change_watchers_) {
    watcher->OnVisibleStoriesChange(visible_stories_.Clone());
  }
}

}  // namespace modular
