// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/focus.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

// Prefix of the keys under which focus entries are stored in the user
// root page. After the prefix follows the device ID.
constexpr char kFocusKeyPrefix[] = "Focus/";

namespace {

bool IsFocusKey(const fidl::Array<uint8_t>& key) {
  constexpr size_t prefix_size = sizeof(kFocusKeyPrefix) - 1;

  // NOTE(mesch): A key that is *only* the prefix, without anything
  // after it, is still not a valid story key. So the key must be
  // truly longer than the prefix.
  return key.size() > prefix_size &&
         0 == memcmp(key.data(), kFocusKeyPrefix, prefix_size);
}

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
  void Run() override { GetEntries(nullptr); }

  void GetEntries(fidl::Array<uint8_t> continuation_token) {
    (*snapshot_)
        ->GetEntries(
            to_array(kFocusKeyPrefix), std::move(continuation_token),
            [this](ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
                   fidl::Array<uint8_t> continuation_token) {
              if (status != ledger::Status::OK &&
                  status != ledger::Status::PARTIAL_RESULT) {
                FTL_LOG(ERROR) << "Ledger status " << status << ".";
                Done(std::move(data_));
                return;
              }

              if (entries.size() == 0) {
                // No existing entries.
                Done(std::move(data_));
                return;
              }

              for (const auto& entry : entries) {
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

              if (status == ledger::Status::PARTIAL_RESULT) {
                GetEntries(std::move(continuation_token));
              } else {
                Done(std::move(data_));
              }
            });
  }

  std::shared_ptr<ledger::PageSnapshotPtr> snapshot_;
  fidl::Array<FocusInfoPtr> data_;
  FTL_DISALLOW_COPY_AND_ASSIGN(QueryCall);
};

FocusHandler::FocusHandler(const fidl::String& device_name,
                           ledger::Page* const page)
    : page_(page),
      page_client_("FocusHandler"),
      page_watcher_binding_(this),
      device_name_(device_name) {
  page_->GetSnapshot(
      page_client_.NewRequest(), page_watcher_binding_.NewBinding(),
      [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "Page.GetSnapshot() status: " << status;
        }
      });
}

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
  new QueryCall(&operation_queue_, page_client_.page_snapshot(), callback);
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
  focus_info->device_id = device_name_;
  focus_info->focused_story_id = story_id;
  focus_info->last_focus_change_timestamp =
      ftl::TimePoint::Now().ToEpochDelta().ToSeconds();

  std::string json;
  XdrWrite(&json, &focus_info, XdrFocusInfo);

  // Focus watchers are notified from the page watcher notification.
  std::string key{kFocusKeyPrefix + device_name_};
  page_->PutWithPriority(
      to_array(key), to_array(json), ledger::Priority::EAGER,
      [this](ledger::Status status) {
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

void FocusHandler::OnChange(ledger::PageChangePtr page,
                            ledger::ResultState result_state,
                            const OnChangeCallback& callback) {
  for (auto& entry : page->changes) {
    if (!IsFocusKey(entry->key)) {
      continue;
    }

    std::string value;
    if (!mtl::StringFromVmo(entry->value, &value)) {
      FTL_LOG(ERROR) << "VMO for key " << to_string(entry->key)
                     << " couldn't be copied.";
      return;
    }

    auto focus_info = FocusInfo::New();
    if (!XdrRead(value, &focus_info, XdrFocusInfo)) {
      continue;
    }

    for (const auto& watcher : change_watchers_) {
      watcher->OnFocusChange(focus_info.Clone());
    }
  }

  if (result_state != ledger::ResultState::COMPLETED &&
      result_state != ledger::ResultState::PARTIAL_COMPLETED) {
    callback(nullptr);
  } else {
    callback(page_client_.NewRequest());
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
