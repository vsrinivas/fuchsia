// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_storage_impl.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/services/story/story_data.fidl.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

void XdrLinkPath(XdrContext* const xdr, LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrModuleData(XdrContext* const xdr, ModuleData* const data) {
  xdr->Field("url", &data->url);
  xdr->Field("module_path", &data->module_path);
  xdr->Field("default_link_path", &data->default_link_path, XdrLinkPath);
}

void XdrPerDeviceStoryInfo(XdrContext* const xdr,
                           PerDeviceStoryInfo* const info) {
  xdr->Field("device", &info->device_id);
  xdr->Field("id", &info->story_id);
  xdr->Field("time", &info->timestamp);
  xdr->Field("state", &info->state);
}

void XdrStoryContextLog(XdrContext* const xdr, StoryContextLog* const data) {
  xdr->Field("context", &data->context);
  xdr->Field("device_id", &data->device_id);
  xdr->Field("time", &data->time);
  xdr->Field("signal", &data->signal);
}

}  // namespace

class StoryStorageImpl::ReadLinkDataCall : Operation<fidl::String> {
 public:
  ReadLinkDataCall(OperationContainer* const container,
                   ledger::Page* const page,
                   const LinkPathPtr& link_path,
                   ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        link_key_(MakeLinkKey(link_path)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    page_->GetSnapshot(page_snapshot_.NewRequest(), nullptr, nullptr,
                       [this, flow](ledger::Status status) {
                         if (status != ledger::Status::OK) {
                           FTL_LOG(ERROR) << "ReadLinkDataCall() " << link_key_
                                          << " Page.GetSnapshot() " << status;
                           return;
                         }

                         Cont(flow);
                       });
  }

  void Cont(FlowToken flow) {
    page_snapshot_->Get(to_array(link_key_), [this, flow](ledger::Status status,
                                                          mx::vmo value) {
      if (status != ledger::Status::OK) {
        if (status != ledger::Status::KEY_NOT_FOUND) {
          // It's expected that the key is not found when the link is
          // accessed for the first time. Don't log an error then.
          FTL_LOG(ERROR) << "ReadLinkDataCall() " << link_key_
                         << " PageSnapshot.Get() " << status;
        }
        return;
      }

      std::string value_as_string;
      if (value) {
        if (!mtl::StringFromVmo(value, &value_as_string)) {
          FTL_LOG(ERROR) << "ReadLinkDataCall() " << link_key_
                         << " Unable to extract data.";
          return;
        }
      }

      result_.Swap(&value_as_string);
    });
  }

  ledger::Page* const page_;  // not owned
  ledger::PageSnapshotPtr page_snapshot_;
  const std::string link_key_;
  fidl::String result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadLinkDataCall);
};

class StoryStorageImpl::WriteLinkDataCall : Operation<> {
 public:
  WriteLinkDataCall(OperationContainer* const container,
                    ledger::Page* const page,
                    const LinkPathPtr& link_path,
                    fidl::String data,
                    ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        link_key_(MakeLinkKey(link_path)),
        data_(std::move(data)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    page_->Put(to_array(link_key_), to_array(data_),
               [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR)
                       << "WriteLinkDataCall() link key =" << link_key_
                       << ", Page.Put() " << status;
                 }
               });
  }

  ledger::Page* const page_;  // not owned
  const std::string link_key_;
  fidl::String data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteLinkDataCall);
};

class StoryStorageImpl::ReadModuleDataCall : Operation<ModuleDataPtr> {
 public:
  ReadModuleDataCall(OperationContainer* const container,
                     ledger::Page* const page,
                     const fidl::Array<fidl::String>& module_path,
                     ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        module_path_(module_path.Clone()) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    page_->GetSnapshot(page_snapshot_.NewRequest(), nullptr, nullptr,
                       [this, flow](ledger::Status status) {
                         if (status != ledger::Status::OK) {
                           FTL_LOG(ERROR) << "ReadModuleDataCall() "
                                          << "Page.GetSnapshot() " << status;
                           return;
                         }

                         Cont(flow);
                       });
  }

  void Cont(FlowToken flow) {
    page_snapshot_->Get(
        to_array(MakeModuleKey(module_path_)),
        [this, flow](ledger::Status status, mx::vmo value) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "ReadModuleDataCall() "
                           << " PageSnapshot.GetEntries() " << status;
            return;
          }

          std::string value_as_string;
          if (!mtl::StringFromVmo(value, &value_as_string)) {
            FTL_LOG(ERROR) << "Unable to extract data.";
            return;
          }

          if (!XdrRead(value_as_string, &result_, XdrModuleData)) {
            result_.reset();
            return;
          }

          FTL_DCHECK(!result_.is_null());
        });
  }

  ledger::Page* page_;
  ledger::PageSnapshotPtr page_snapshot_;
  const fidl::Array<fidl::String> module_path_;
  std::vector<ledger::EntryPtr> entries_;
  ModuleDataPtr result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadModuleDataCall);
};

class StoryStorageImpl::ReadAllModuleDataCall
    : Operation<fidl::Array<ModuleDataPtr>> {
 public:
  ReadAllModuleDataCall(OperationContainer* const container,
                        ledger::Page* const page,
                        ResultCall result_call)
      : Operation(container, std::move(result_call)), page_(page) {
    data_.resize(0);
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &data_};

    page_->GetSnapshot(page_snapshot_.NewRequest(),
                       to_array(kModuleKeyPrefix), nullptr,
                       [this, flow](ledger::Status status) {
                         if (status != ledger::Status::OK) {
                           FTL_LOG(ERROR) << "ReadAllModuleDataCall() "
                                          << "Page.GetSnapshot() " << status;
                           return;
                         }

                         Cont1(flow);
                       });
  }

  void Cont1(FlowToken flow) {
    GetEntries(page_snapshot_.get(), nullptr, &entries_,
               nullptr /* next_token */, [this, flow](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR) << "ReadAllModuleDataCall() "
                                  << "GetEntries() " << status;
                   return;
                 }

                 Cont2(flow);
               });
  }

  void Cont2(FlowToken flow) {
    for (auto& entry : entries_) {
      std::string value_as_string;
      if (!mtl::StringFromVmo(entry->value, &value_as_string)) {
        FTL_LOG(ERROR) << "ReadModuleDataCall() "
                       << "Unable to extract data.";
        continue;
      }

      ModuleDataPtr module_data;
      if (!XdrRead(value_as_string, &module_data, XdrModuleData)) {
        continue;
      }

      FTL_DCHECK(!module_data.is_null());

      data_.push_back(std::move(module_data));
    }
  }

  ledger::Page* page_;  // not owned
  ledger::PageSnapshotPtr page_snapshot_;
  std::vector<ledger::EntryPtr> entries_;
  fidl::Array<ModuleDataPtr> data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadAllModuleDataCall);
};

class StoryStorageImpl::WriteModuleDataCall : Operation<> {
 public:
  WriteModuleDataCall(OperationContainer* const container,
                      ledger::Page* const page,
                      const fidl::Array<fidl::String>& module_path,
                      const fidl::String& module_url,
                      const modular::LinkPathPtr& link_path,
                      ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        module_path_(module_path.Clone()),
        module_url_(module_url),
        link_path_(link_path.Clone()) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    auto module_data = ModuleData::New();
    module_data->url = module_url_;
    module_data->module_path = module_path_.Clone();
    module_data->default_link_path = std::move(link_path_);

    std::string json;
    XdrWrite(&json, &module_data, XdrModuleData);

    page_->PutWithPriority(
        to_array(MakeModuleKey(module_path_)), to_array(json),
        ledger::Priority::EAGER, [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "WriteModuleDataCall() " << module_url_
                           << " Page.PutWithPriority() " << status;
          }
        });
  }

  ledger::Page* const page_;  // not owned
  const fidl::Array<fidl::String> module_path_;
  const fidl::String module_url_;
  modular::LinkPathPtr link_path_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteModuleDataCall);
};

class StoryStorageImpl::WriteDeviceDataCall : Operation<> {
 public:
  WriteDeviceDataCall(OperationContainer* const container,
                      ledger::Page* const page,
                      const std::string& story_id,
                      const std::string& device_id,
                      StoryState state,
                      ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        story_id_(story_id),
        device_id_(device_id),
        state_(state) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    auto per_device = PerDeviceStoryInfo::New();
    per_device->device_id = device_id_;
    per_device->story_id = story_id_;
    per_device->timestamp = time(nullptr);
    per_device->state = state_;

    std::string json;
    XdrWrite(&json, &per_device, XdrPerDeviceStoryInfo);

    page_->PutWithPriority(
        to_array(MakePerDeviceKey(per_device->device_id)), to_array(json),
        ledger::Priority::EAGER, [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "WriteDeviceDataCall() " << device_id_
                           << " Page.PutWithPriority() " << status;
          }
        });
  }

  ledger::Page* const page_;  // not owned
  const std::string story_id_;
  const std::string device_id_;
  StoryState state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteDeviceDataCall);
};

class StoryStorageImpl::StoryContextLogCall : Operation<> {
 public:
  StoryContextLogCall(OperationContainer* const container,
                      ledger::Page* const page,
                      StoryContextLogPtr log_entry)
      : Operation(container, [] {}),
        page_(page),
        log_entry_(std::move(log_entry)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // We write the current context when the story was created.
    std::string json;
    XdrWrite(&json, &log_entry_, XdrStoryContextLog);

    page_->PutWithPriority(
        to_array(MakeStoryContextLogKey(log_entry_->signal, log_entry_->time)),
        to_array(json),
        ledger::Priority::EAGER, [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "StoryContextLogCall"
                           << " Page.PutWithPriority() " << status;
          }
        });
  }

  ledger::Page* const page_;  // not owned
  StoryContextLogPtr log_entry_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryContextLogCall);
};

StoryStorageImpl::StoryStorageImpl(ledger::Page* const story_page)
    : PageClient("StoryStorageImpl", story_page, kLinkKeyPrefix),
      story_page_(story_page) {}

StoryStorageImpl::~StoryStorageImpl() = default;

void StoryStorageImpl::ReadLinkData(const LinkPathPtr& link_path,
                                    const DataCallback& callback) {
  new ReadLinkDataCall(&operation_queue_, story_page_, link_path, callback);
}

void StoryStorageImpl::WriteLinkData(const LinkPathPtr& link_path,
                                     const fidl::String& data,
                                     const SyncCallback& callback) {
  new WriteLinkDataCall(&operation_queue_, story_page_, link_path, data,
                        callback);
}

void StoryStorageImpl::ReadModuleData(
    const fidl::Array<fidl::String>& module_path,
    const ModuleDataCallback& callback) {
  new ReadModuleDataCall(&operation_queue_, story_page_, module_path, callback);
}

void StoryStorageImpl::ReadAllModuleData(
    const AllModuleDataCallback& callback) {
  new ReadAllModuleDataCall(&operation_queue_, story_page_, callback);
}

void StoryStorageImpl::WriteModuleData(
    const fidl::Array<fidl::String>& module_path,
    const fidl::String& module_url,
    const LinkPathPtr& link_path,
    const SyncCallback& callback) {
  new WriteModuleDataCall(&operation_queue_, story_page_, module_path,
                          module_url, link_path, callback);
}

void StoryStorageImpl::WatchLink(const LinkPathPtr& link_path,
                                 LinkImpl* const impl,
                                 const DataCallback& watcher) {
  watchers_.emplace_back(WatcherEntry{MakeLinkKey(link_path), impl, watcher});
}

void StoryStorageImpl::DropWatcher(LinkImpl* const impl) {
  auto f = std::find_if(watchers_.begin(), watchers_.end(),
                        [impl](auto& entry) { return entry.impl == impl; });
  FTL_DCHECK(f != watchers_.end());
  watchers_.erase(f);
}

void StoryStorageImpl::WriteDeviceData(const std::string& story_id,
                                       const std::string& device_id,
                                       StoryState state,
                                       const SyncCallback& callback) {
  new WriteDeviceDataCall(&operation_queue_, story_page_, story_id, device_id,
                          state, callback);
}

void StoryStorageImpl::Log(StoryContextLogPtr log_entry) {
  new StoryContextLogCall(&operation_queue_, story_page_, std::move(log_entry));
}

void StoryStorageImpl::Sync(const SyncCallback& callback) {
  new SyncCall(&operation_queue_, callback);
}

void StoryStorageImpl::OnChange(const std::string& key,
                                const std::string& value) {
  for (auto& watcher_entry : watchers_) {
    if (key == watcher_entry.key) {
      watcher_entry.watcher(value);
    }
  }
}

}  // namespace modular
