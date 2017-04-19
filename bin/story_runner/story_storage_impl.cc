// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_storage_impl.h"

#include <memory>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/util/string_escape.h"
#include "lib/ftl/strings/join_strings.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

// TODO(mesch): Duplicated from story_provider_impl.cc.

// Retrieves all entries from the given snapshot and calls the given
// callback with the final status.
void GetEntries(ledger::PageSnapshotPtr* const snapshot,
                std::vector<ledger::EntryPtr>* const entries,
                fidl::Array<uint8_t> token,
                std::function<void(ledger::Status)> callback) {
  (*snapshot)->GetEntries(to_array(kModuleKeyPrefix), std::move(token), [
    snapshot, entries, callback = std::move(callback)
  ](ledger::Status status, auto new_entries, auto next_token) mutable {
    if (status != ledger::Status::OK &&
        status != ledger::Status::PARTIAL_RESULT) {
      callback(status);
      return;
    }
    for (auto& entry : new_entries) {
      entries->push_back(std::move(entry));
    }
    if (status == ledger::Status::OK) {
      callback(ledger::Status::OK);
      return;
    }
    GetEntries(snapshot, entries, std::move(next_token), std::move(callback));
  });
}

void XdrModuleData(XdrContext* const xdr, ModuleData* const data) {
  xdr->Field("url", &data->url);
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link", &data->link);
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
        link_path_(MakeLinkKey(module_path, link_id)),
        link_id_(link_id) {
    Ready();
  }

 private:
  void Run() override {
    FTL_LOG(INFO) << "ReadLinkDataCall, link_path_ = " << link_path_;
    (*page_snapshot_)
        ->Get(to_array(link_path_),
              [this](ledger::Status status, mx::vmo value) {
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
        link_path_(MakeLinkKey(module_path, link_id)),
        link_id_(std::move(link_id)),
        data_(std::move(data)) {
    Ready();
  }

 private:
  void Run() override {
    FTL_LOG(INFO) << "WriteLinkDataCall, link_path_ = " << link_path_;
    page_->Put(to_array(link_path_), to_array(data_),
               [this](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR)
                       << "WriteLinkDataCall() link_path_=" << link_path_
                       << ", link_id_=" << link_id_ << " Page.Put() " << status;
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

class StoryStorageImpl::ReadModuleDataCall
    : Operation<fidl::Array<ModuleDataPtr>> {
 public:
  ReadModuleDataCall(OperationContainer* const container,
                     std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot,
                     ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_snapshot_(std::move(page_snapshot)) {
    data_.resize(0);
    Ready();
  }

 private:
  void Run() override {
    GetEntries(page_snapshot_.get(), &entries_, nullptr /* next_token */,
               [this](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR) << "ReadModuleDataCall() "
                                  << " PageSnapshot.GetEntries() " << status;
                   Done(std::move(data_));
                   return;
                 }

                 for (auto& entry : entries_) {
                   std::string value_as_string;
                   if (!mtl::StringFromVmo(entry->value, &value_as_string)) {
                     FTL_LOG(ERROR) << "Unable to extract data.";
                     continue;
                   }

                   ModuleDataPtr module_data;
                   if (!XdrRead(value_as_string, &module_data, XdrModuleData)) {
                     continue;
                   }

                   FTL_DCHECK(!module_data.is_null());

                   data_.push_back(std::move(module_data));
                 }

                 Done(std::move(data_));
               });
  }

  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot_;
  std::vector<ledger::EntryPtr> entries_;
  fidl::Array<ModuleDataPtr> data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadModuleDataCall);
};

class StoryStorageImpl::WriteModuleDataCall : Operation<void> {
 public:
  WriteModuleDataCall(OperationContainer* const container,
                      ledger::Page* const page,
                      const fidl::String& module_name,
                      const fidl::String& module_url,
                      const fidl::String& link_name,
                      ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        module_name_(module_name),
        module_url_(module_url),
        link_name_(link_name) {
    Ready();
  }

 private:
  void Run() override {
    auto module_data = ModuleData::New();
    module_data->url = module_url_;
    module_data->module_path = fidl::Array<fidl::String>::New(0);
    module_data->module_path.push_back(module_name_);
    module_data->link = link_name_;

    std::string json;
    XdrWrite(&json, &module_data, XdrModuleData);

    page_->PutWithPriority(
        to_array(MakeModuleKey(module_name_)), to_array(json),
        ledger::Priority::EAGER, [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "WriteModuleDataCall() " << module_url_
                           << " Page.PutWithPriority() " << status;
          }
          Done();
        });
  }

  ledger::Page* const page_;  // not owned
  const fidl::String module_name_;
  const fidl::String module_url_;
  const fidl::String link_name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteModuleDataCall);
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

void StoryStorageImpl::ReadLinkData(
    const fidl::Array<fidl::String>& module_path,
    const fidl::String& link_id,
    const DataCallback& callback) {
  new ReadLinkDataCall(&operation_queue_, story_client_.page_snapshot(),
                       module_path, link_id, callback);
}

void StoryStorageImpl::WriteLinkData(
    const fidl::Array<fidl::String>& module_path,
    const fidl::String& link_id,
    const fidl::String& data,
    const SyncCallback& callback) {
  new WriteLinkDataCall(&operation_queue_, story_page_, module_path, link_id,
                        data, callback);
}

void StoryStorageImpl::ReadModuleData(const ModuleDataCallback& callback) {
  new ReadModuleDataCall(&operation_queue_, story_client_.page_snapshot(),
                         callback);
}

void StoryStorageImpl::WriteModuleData(const fidl::String& module_name,
                                       const fidl::String& module_url,
                                       const fidl::String& link_name,
                                       const SyncCallback& callback) {
  new WriteModuleDataCall(&operation_queue_, story_page_, module_name,
                          module_url, link_name, callback);
}

void StoryStorageImpl::WatchLink(const fidl::Array<fidl::String>& module_path,
                                 const fidl::String& link_name,
                                 const DataCallback& watcher) {
  fidl::String key{MakeLinkKey(module_path, link_name)};
  watchers_.emplace_back(std::make_pair(key, watcher));
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
