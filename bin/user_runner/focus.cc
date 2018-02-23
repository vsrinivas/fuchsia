// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/focus.h"

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/time/time_point.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"
#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

namespace {

// Serialization and deserialization of FocusInfo to and from JSON.
void XdrFocusInfo(XdrContext* const xdr, FocusInfo* const data) {
  xdr->Field("device_id", &data->device_id);
  xdr->Field("focused_story_id", &data->focused_story_id);
  xdr->Field("last_focus_timestamp", &data->last_focus_change_timestamp);
}

}  // namespace

FocusHandler::FocusHandler(const f1dl::String& device_id,
                           LedgerClient* const ledger_client,
                           LedgerPageId page_id)
    : PageClient("FocusHandler",
                 ledger_client,
                 std::move(page_id),
                 kFocusKeyPrefix),
      device_id_(device_id) {}

FocusHandler::~FocusHandler() = default;

void FocusHandler::AddProviderBinding(
    f1dl::InterfaceRequest<FocusProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void FocusHandler::AddControllerBinding(
    f1dl::InterfaceRequest<FocusController> request) {
  controller_bindings_.AddBinding(this, std::move(request));
}

// |FocusProvider|
void FocusHandler::Query(const QueryCallback& callback) {
  new ReadAllDataCall<FocusInfo, f1dl::InlinedStructPtr<FocusInfo>>(
      &operation_queue_, page(), kFocusKeyPrefix, XdrFocusInfo, callback);
}

// |FocusProvider|
void FocusHandler::Watch(f1dl::InterfaceHandle<FocusWatcher> watcher) {
  change_watchers_.push_back(watcher.Bind());
}

// |FocusProvider|
void FocusHandler::Request(const f1dl::String& story_id) {
  for (const auto& watcher : request_watchers_) {
    watcher->OnFocusRequest(story_id);
  }
}

// |FocusProvider|
void FocusHandler::Duplicate(f1dl::InterfaceRequest<FocusProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

// |FocusController|
void FocusHandler::Set(const f1dl::String& story_id) {
  FocusInfoPtr data = FocusInfo::New();
  data->device_id = device_id_;
  data->focused_story_id = story_id;
  data->last_focus_change_timestamp = time(nullptr);

  new WriteDataCall<FocusInfo, f1dl::InlinedStructPtr<FocusInfo>>(
      &operation_queue_, page(), MakeFocusKey(device_id_), XdrFocusInfo,
      std::move(data), [] {});
}

// |FocusController|
void FocusHandler::WatchRequest(
    f1dl::InterfaceHandle<FocusRequestWatcher> watcher) {
  request_watchers_.push_back(watcher.Bind());
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
    : visible_stories_(f1dl::Array<f1dl::String>::New(0)) {}

VisibleStoriesHandler::~VisibleStoriesHandler() = default;

void VisibleStoriesHandler::AddProviderBinding(
    f1dl::InterfaceRequest<VisibleStoriesProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void VisibleStoriesHandler::AddControllerBinding(
    f1dl::InterfaceRequest<VisibleStoriesController> request) {
  controller_bindings_.AddBinding(this, std::move(request));
}

void VisibleStoriesHandler::Query(const QueryCallback& callback) {
  callback(visible_stories_.Clone());
}

void VisibleStoriesHandler::Watch(
    f1dl::InterfaceHandle<VisibleStoriesWatcher> watcher) {
  change_watchers_.push_back(watcher.Bind());
}

void VisibleStoriesHandler::Duplicate(
    f1dl::InterfaceRequest<VisibleStoriesProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void VisibleStoriesHandler::Set(f1dl::Array<f1dl::String> story_ids) {
  visible_stories_ = std::move(story_ids);
  for (const auto& watcher : change_watchers_) {
    watcher->OnVisibleStoriesChange(visible_stories_.Clone());
  }
}

}  // namespace modular
