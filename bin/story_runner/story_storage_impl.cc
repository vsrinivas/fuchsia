// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_storage_impl.h"

#include <memory>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/util/string_escape.h"
#include "lib/ftl/strings/join_strings.h"
#include "lib/ftl/strings/escape.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {
namespace {

std::string LinkNameToLedgerKey(const fidl::Array<fidl::String>& path,
                                const fidl::String& link_id) {
  std::vector<std::string> escaped_path;
  escaped_path.reserve(path.size());

  // Escape both '/' and ':':
  //  - / is used to separate the module path components
  //  - : is used to separate module path components and the link name
  for (const auto& item : path) {
    escaped_path.push_back(StringEscape(item.get(), "/:", '\\'));
  }

  return ftl::JoinStrings(escaped_path, "/") + ":" +
         StringEscape(link_id.get(), "/:", '\\');
}

}  // namespace

class StoryStorageImpl::ReadLinkDataCall : Operation<fidl::String> {
 public:
  ReadLinkDataCall(OperationContainer* const container,
                   std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot,
                   const fidl::Array<fidl::String>& module_path,
                   const fidl::String& link_id,
                   ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_snapshot_(std::move(page_snapshot)),
        link_path_(LinkNameToLedgerKey(module_path, link_id)),
        link_id_(link_id) {
    Ready();
  }

 private:
  void Run() override {
    FTL_LOG(INFO) << "ReadLinkDataCall, link_path_ = " << link_path_;
    std::string key{kLinkKeyPrefix + link_path_};
    (*page_snapshot_)
        ->Get(to_array(key), [this](ledger::Status status, mx::vmo value) {
          if (status != ledger::Status::OK) {
            if (status != ledger::Status::KEY_NOT_FOUND) {
              // It's expected that the key is not found when the link
              // is accessed for the first time. Don't log an error
              // then.
              FTL_LOG(ERROR) << "ReadLinkDataCall() " << link_id_
                             << " PageSnapshot.Get() " << status;
            }
            Done(fidl::String());
            return;
          }

          std::string value_as_string;
          if (value) {
            if (!mtl::StringFromVmo(value, &value_as_string)) {
              FTL_LOG(ERROR) << "Unable to extract data.";
              Done(nullptr);
              return;
            }
          }
          fidl::String result;
          result.Swap(&value_as_string);
          Done(result);
        });
  }

  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot_;
  const std::string link_path_;
  const fidl::String link_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadLinkDataCall);
};

class StoryStorageImpl::WriteLinkDataCall : Operation<void> {
 public:
  WriteLinkDataCall(OperationContainer* const container,
                    ledger::Page* const page,
                    const fidl::Array<fidl::String>& module_path,
                    fidl::String link_id,
                    fidl::String data,
                    ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        link_path_(LinkNameToLedgerKey(module_path, link_id)),
        link_id_(std::move(link_id)),
        data_(std::move(data)) {
    Ready();
  }

 private:
  void Run() override {
    FTL_LOG(INFO) << "WriteLinkDataCall, link_path_ = " << link_path_;
    std::string key{kLinkKeyPrefix + link_path_};
    page_->Put(to_array(key), to_array(data_), [this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "WriteLinkDataCall() " << link_id_ << " Page.Put() "
                       << status;
      }
      Done();
    });
  }

  ledger::Page* const page_;  // not owned
  const std::string link_path_;
  fidl::String link_id_;
  fidl::String data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteLinkDataCall);
};

StoryStorageImpl::StoryStorageImpl(ledger::Page* const story_page)
    : page_watcher_binding_(this),
      story_page_(story_page),
      story_client_("StoryStorageImpl") {
  FTL_DCHECK(story_page_);
  story_page_->GetSnapshot(
      story_client_.NewRequest(), page_watcher_binding_.NewBinding(),
      [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR)
              << "StoryStorageImpl() failed call to Ledger.GetSnapshot() "
              << status;
        }
      });
}

StoryStorageImpl::~StoryStorageImpl() = default;

void StoryStorageImpl::ReadLinkData(const fidl::Array<fidl::String>& module_path,
                                    const fidl::String& link_id,
                                    const DataCallback& callback) {
  new ReadLinkDataCall(&operation_queue_, story_client_.page_snapshot(),
                       module_path, link_id, callback);
}

void StoryStorageImpl::WriteLinkData(
    const fidl::Array<fidl::String>& module_path, const fidl::String& link_id,
    const fidl::String& data, const SyncCallback& callback) {
  new WriteLinkDataCall(&operation_queue_, story_page_, module_path, link_id,
                        data, callback);
}

void StoryStorageImpl::WatchLink(const fidl::String& link_id,
                                 const DataCallback& watcher) {
  watchers_.emplace_back(std::make_pair(link_id, watcher));
}

void StoryStorageImpl::Sync(const SyncCallback& callback) {
  new SyncCall(&operation_queue_, callback);
}

// |PageWatcher|
void StoryStorageImpl::OnChange(ledger::PageChangePtr page,
                                ledger::ResultState result_state,
                                const OnChangeCallback& callback) {
  if (!page.is_null() && !page->changes.is_null()) {
    for (auto& entry : page->changes) {
      const fidl::String link_id = to_string(entry->key);
      for (auto& watcher_entry : watchers_) {
        if (link_id == watcher_entry.first) {
          std::string value_as_string;
          if (!mtl::StringFromVmo(entry->value, &value_as_string)) {
            FTL_LOG(ERROR) << "Unable to extract data.";
            continue;
          }
          watcher_entry.second(value_as_string);
        }
      }
    }
  }

  // Every time we receive a group of OnChange notifications, we update the
  // root page snapshot so we see the current state. Note that pending
  // Operation instances hold on to the previous value until they finish. New
  // Operation instances created after the update receive the new snapshot.
  //
  // For continued updates, we only request the snapshot once, in the
  // last OnChange() notification.
  callback(story_client_.Update(result_state));
}

}  // namespace modular
