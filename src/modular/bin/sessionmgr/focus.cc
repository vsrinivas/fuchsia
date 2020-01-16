// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/focus.h"

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/time/time_point.h"
#include "src/modular/bin/sessionmgr/storage/constants_and_utils.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/fidl/json_xdr.h"
#include "src/modular/lib/ledger_client/operations.h"

namespace modular {

namespace {

// Serialization and deserialization of fuchsia::modular::FocusInfo to and from
// JSON.
void XdrFocusInfo_v1(XdrContext* const xdr, fuchsia::modular::FocusInfo* const data) {
  xdr->Field("device_id", &data->device_id);
  xdr->Field("focused_story_id", &data->focused_story_id);
  xdr->Field("last_focus_timestamp", &data->last_focus_change_timestamp);
}

void XdrFocusInfo_v2(XdrContext* const xdr, fuchsia::modular::FocusInfo* const data) {
  if (!xdr->Version(2)) {
    return;
  }
  xdr->Field("device_id", &data->device_id);
  xdr->Field("focused_story_id", &data->focused_story_id);
  xdr->Field("last_focus_timestamp", &data->last_focus_change_timestamp);
}

constexpr XdrFilterType<fuchsia::modular::FocusInfo> XdrFocusInfo[] = {
    XdrFocusInfo_v2,
    XdrFocusInfo_v1,
    nullptr,
};

}  // namespace

FocusHandler::FocusHandler(fidl::StringPtr device_id, LedgerClient* const ledger_client,
                           LedgerPageId page_id)
    : PageClient("FocusHandler", ledger_client, std::move(page_id), kFocusKeyPrefix),
      device_id_(device_id) {}

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
  operation_queue_.Add(std::make_unique<ReadAllDataCall<fuchsia::modular::FocusInfo>>(
      page(), kFocusKeyPrefix, XdrFocusInfo,
      [callback = std::move(callback)](std::vector<fuchsia::modular::FocusInfo> infos) {
        callback(std::move(infos));
      }));
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
  fuchsia::modular::FocusInfoPtr data = fuchsia::modular::FocusInfo::New();
  data->device_id = device_id_.value_or("");
  data->focused_story_id = story_id;
  data->last_focus_change_timestamp = time(nullptr);

  operation_queue_.Add(std::make_unique<WriteDataCall<fuchsia::modular::FocusInfo>>(
      page(), MakeFocusKey(device_id_), XdrFocusInfo, std::move(data), [] {}));
}

// |fuchsia::modular::FocusController|
void FocusHandler::WatchRequest(
    fidl::InterfaceHandle<fuchsia::modular::FocusRequestWatcher> watcher) {
  request_watchers_.push_back(watcher.Bind());
}

// |PageClient|
void FocusHandler::OnPageChange(const std::string& /*key*/, const std::string& value) {
  auto focus_info = fuchsia::modular::FocusInfo::New();
  if (!XdrRead(value, &focus_info, XdrFocusInfo)) {
    return;
  }

  for (const auto& watcher : change_watchers_) {
    watcher->OnFocusChange(CloneOptional(focus_info));
  }
}

}  // namespace modular
