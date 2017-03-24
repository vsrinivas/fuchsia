// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/focus.h"

#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/time/time_point.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

constexpr char kUserFocusLedger[] = "user-focus";
constexpr char kFocusedStoryIdKey[] = "focused-story-id";
constexpr char kLastFocusChangeTimestampKey[] = "last-focus-change-timestamp";

std::string SerializeToLedgerValue(const std::string& focused_story_id,
                                   uint64_t timestamp) {
  rapidjson::Document doc;
  auto& allocator = doc.GetAllocator();
  doc.SetObject();
  doc.AddMember(kFocusedStoryIdKey,
                rapidjson::Value(focused_story_id, allocator), allocator);
  doc.AddMember(kLastFocusChangeTimestampKey,
                rapidjson::Value().SetUint64(timestamp), allocator);
  return modular::JsonValueToString(doc);
}

FocusInfoPtr CreateFocusInfo(const std::string& key, const std::string& value) {
  rapidjson::Document value_doc;
  value_doc.Parse(value);
  FTL_DCHECK(value_doc.HasMember(kFocusedStoryIdKey));
  FTL_DCHECK(value_doc[kFocusedStoryIdKey].IsString());
  FTL_DCHECK(value_doc.HasMember(kLastFocusChangeTimestampKey));
  FTL_DCHECK(value_doc[kLastFocusChangeTimestampKey].IsUint64());

  auto focus_info = FocusInfo::New();
  focus_info->device_id = key;
  focus_info->focused_story_id = value_doc[kFocusedStoryIdKey].GetString();
  focus_info->last_focus_change_timestamp =
      value_doc[kLastFocusChangeTimestampKey].GetUint64();
  return focus_info;
}

class GetLedgerSnapshotCall : public Operation<void> {
 public:
  GetLedgerSnapshotCall(
      OperationContainer* const container,
      ledger::PageSnapshotPtr snapshot,
      std::unordered_map<std::string, std::string>* const ledger_map)
      : Operation(container, [] {}),
        snapshot_(std::move(snapshot)),
        ledger_map_(ledger_map) {
    Ready();
  }

  void Run() override {
    snapshot_->GetEntries(
        nullptr, nullptr,
        [this](ledger::Status status, fidl::Array<ledger::EntryPtr> entries,
               fidl::Array<uint8_t> continuation_token) {
          // TODO(alhaad): It is possible that entries in ledger snapshot are
          // played in multiple runs. Handle it!
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "Ledger status " << status << "."
                           << "This maybe because of the TODO above";
            Done();
            return;
          }

          if (entries.size() == 0) {
            // No existing entries.
            Done();
            return;
          }

          for (const auto& entry : entries) {
            std::string key(reinterpret_cast<const char*>(entry->key.data()),
                            entry->key.size());
            std::string value;
            if (!mtl::StringFromVmo(entry->value, &value)) {
              FTL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
              return;
            }

            ledger_map_->insert({key, value});
          }
          Done();
        });
  }

 private:
  ledger::PageSnapshotPtr snapshot_;
  std::unordered_map<std::string, std::string>* const ledger_map_;
  FTL_DISALLOW_COPY_AND_ASSIGN(GetLedgerSnapshotCall);
};

}  // namespace

FocusHandler::FocusHandler(const fidl::String& device_name,
                           ledger::LedgerRepository* ledger_repository)
    : page_watcher_binding_(this), device_name_(device_name) {
  auto error_handler = [](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
    }
  };

  ledger_repository->GetLedger(to_array(kUserFocusLedger), ledger_.NewRequest(),
                               error_handler);

  ledger_->GetRootPage(page_.NewRequest(), error_handler);

  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), page_watcher_binding_.NewBinding(),
                     error_handler);

  new GetLedgerSnapshotCall(&operation_queue_, std::move(snapshot),
                            &ledger_map_);
}

void FocusHandler::Query(const QueryCallback& callback) {
  new SyncCall(&operation_queue_, [this, callback] {
    auto focused_stories = fidl::Array<FocusInfoPtr>::New(0);
    for (const auto& kv : ledger_map_) {
      auto focus_info = CreateFocusInfo(kv.first, kv.second);
      focused_stories.push_back(std::move(focus_info));
    }
    callback(std::move(focused_stories));
  });
}

void FocusHandler::Set(const fidl::String& story_id) {
  uint64_t seconds_since_epoch =
      ftl::TimePoint::Now().ToEpochDelta().ToSeconds();
  // Add the value to ledger. We'll update |device_name_to_focus_info_| once we
  // get an update back.
  page_->PutWithPriority(
      to_array(device_name_),
      to_array(SerializeToLedgerValue(story_id, seconds_since_epoch)),
      ledger::Priority::EAGER, [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "Ledger operation returned status: " << status;
          return;
        }
      });
  FTL_LOG(INFO) << "Setting focus to story_id: " << story_id;
}

void FocusHandler::OnChange(ledger::PageChangePtr page,
                            ledger::ResultState result_state,
                            const OnChangeCallback& callback) {
  for (auto& entry : page->changes) {
    std::string key(reinterpret_cast<const char*>(entry->key.data()),
                    entry->key.size());
    std::string value;
    if (!mtl::StringFromVmo(entry->value, &value)) {
      FTL_LOG(ERROR) << "VMO for key " << key << " couldn't be copied.";
      return;
    }

    ledger_map_[key] = value;
    for (const auto& watcher : change_watchers_) {
      watcher->OnFocusChange(CreateFocusInfo(key, value));
    }
  }
  callback(nullptr);
}

}  // namespace modular
