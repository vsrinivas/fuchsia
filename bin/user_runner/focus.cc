// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/focus.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/time/time_point.h"
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

// Asynchronous operations of this service.

class FocusHandler::QueryCall : Operation<fidl::Array<FocusInfoPtr>> {
 public:
  QueryCall(OperationContainer* const container,
            std::shared_ptr<ledger::PageSnapshotPtr> const snapshot,
            ResultCall result_call)
      : Operation(container, std::move(result_call)), snapshot_(snapshot) {
    data_.resize(0);  // never return null
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &data_};

    GetEntries((*snapshot_).get(), kFocusKeyPrefix, &entries_,
               nullptr /* next_token */, [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR) << "QueryCall() "
                                  << "GetEntries() " << status;
                   return;
                 }

                 Cont(flow);
               });
  }

  void Cont(FlowToken flow) {
    if (entries_.size() == 0) {
      // No existing entries.
      return;
    }

    for (const auto& entry : entries_) {
      std::string value;
      if (!mtl::StringFromVmo(entry->value, &value)) {
        FTL_LOG(ERROR) << "VMO for key " << to_string(entry->key)
                       << " couldn't be copied.";
        continue;
      }

      auto focus_info = FocusInfo::New();
      if (!XdrRead(value, &focus_info, XdrFocusInfo)) {
        continue;
      }

      data_.push_back(std::move(focus_info));
    }
  }

  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  std::vector<ledger::EntryPtr> entries_;
  fidl::Array<FocusInfoPtr> data_;
  FTL_DISALLOW_COPY_AND_ASSIGN(QueryCall);
};

FocusHandler::FocusHandler(const fidl::String& device_id,
                           ledger::Page* const page)
    : PageClient("FocusHandler", page, kFocusKeyPrefix),
      page_(page),
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

void FocusHandler::Query(const QueryCallback& callback) {
  new QueryCall(&operation_queue_, page_snapshot(), callback);
}

void FocusHandler::Watch(fidl::InterfaceHandle<FocusWatcher> watcher) {
  change_watchers_.push_back(FocusWatcherPtr::Create(std::move(watcher)));
}

void FocusHandler::Request(const fidl::String& story_id) {
  for (const auto& watcher : request_watchers_) {
    watcher->OnRequest(story_id);
  }
}

void FocusHandler::Duplicate(fidl::InterfaceRequest<FocusProvider> request) {
  provider_bindings_.AddBinding(this, std::move(request));
}

void FocusHandler::Set(const fidl::String& story_id) {
  auto focus_info = FocusInfo::New();
  focus_info->device_id = device_id_;
  focus_info->focused_story_id = story_id;
  focus_info->last_focus_change_timestamp =
      ftl::TimePoint::Now().ToEpochDelta().ToSeconds();

  std::string json;
  XdrWrite(&json, &focus_info, XdrFocusInfo);

  // Focus watchers are notified from the page watcher notification.
  page_->PutWithPriority(
      to_array(MakeFocusKey(device_id_)), to_array(json),
      ledger::Priority::EAGER, [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
          return;
        }
      });

  FTL_LOG(INFO) << "Setting focus to story_id: " << story_id;

  // TODO(mesch): What exactly is the guarantee for a sequence of
  // Set() and Query() call? A Query() following Set() does not have
  // to obtain the value that was just Set(), because the two are on
  // different interfaces (FocusProvider vs. FocusController).
  //
  // But a Query() in some combination with Watch() likely must
  // guarantee that the client of the FocusProvider sees all values
  // ever Set() on the FocusController. This doesn't seem the case,
  // unlike the two are combined into one call, analog to ledger's
  // Page.GetSnapshot().
}

void FocusHandler::WatchRequest(
    fidl::InterfaceHandle<FocusRequestWatcher> watcher) {
  request_watchers_.push_back(
      FocusRequestWatcherPtr::Create(std::move(watcher)));
}

void FocusHandler::OnChange(const std::string& key, const std::string& value) {
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
