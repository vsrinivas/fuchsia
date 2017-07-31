// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_storage_impl.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/ledger/operations.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/services/story/story_data.fidl.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

void XdrLinkPath(XdrContext* const xdr, LinkPath* const data) {
  xdr->Field("module_path", &data->module_path);
  xdr->Field("link_name", &data->link_name);
}

void XdrSurfaceRelation(XdrContext* const xdr, SurfaceRelation* const data) {
  xdr->Field("arrangement", &data->arrangement);
  xdr->Field("dependency", &data->dependency);
  xdr->Field("emphasis", &data->emphasis);
}

void XdrModuleData(XdrContext* const xdr, ModuleData* const data) {
  xdr->Field("url", &data->module_url);
  xdr->Field("module_path", &data->module_path);
  // TODO(mesch): Rename the XDR field eventually.
  xdr->Field("default_link_path", &data->link_path, XdrLinkPath);
  xdr->Field("module_source", &data->module_source);

  // TODO(jimbe) Remove error handler after 2017-08-01
  xdr->ReadErrorHandler(
         [data] { data->surface_relation = SurfaceRelation::New(); })
      ->Field("surface_relation", &data->surface_relation, XdrSurfaceRelation);

  // TODO(jimbe) Remove error handler after 2017-08-01
  xdr->ReadErrorHandler([data] { data->module_stopped = false; })
      ->Field("module_stopped", &data->module_stopped);
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
      : Operation("StoryStorageImpl::ReadLinkDataCall",
                  container,
                  std::move(result_call)),
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
      : Operation("StoryStorageImpl::WriteLinkDataCall",
                  container,
                  std::move(result_call)),
        page_(page),
        link_key_(MakeLinkKey(link_path)),
        data_(data) {
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

class StoryStorageImpl::FlushWatchersCall : Operation<> {
 public:
  FlushWatchersCall(OperationContainer* const container,
                    ledger::Page* const page,
                    ResultCall result_call)
      : Operation("StoryStorageImpl::FlushWatchersCall",
                  container,
                  std::move(result_call)),
        page_(page) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // Cf. the documentation in ledger.fidl: Before StartTransaction() returns,
    // all pending watcher notifications on the same connection are guaranteed
    // to have returned. If we execute this Operation after a WriteLinkData()
    // call, then all link watcher notifications are guaranteed to have been
    // received when this Operation is Done().

    page_->StartTransaction([this, flow](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "FlushWatchersCall()"
                         << " Page.StartTransaction() " << status;
          return;
        }

        page_->Commit([this, flow](ledger::Status status) {
            if (status != ledger::Status::OK) {
              FTL_LOG(ERROR) << "FlushWatchersCall()"
                             << " Page.Commit() " << status;
              return;
            }
          });
      });
  }

  ledger::Page* const page_;  // not owned

  FTL_DISALLOW_COPY_AND_ASSIGN(FlushWatchersCall);
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
  new ReadDataCall<ModuleData>(
      &operation_queue_, story_page_, MakeModuleKey(module_path),
      false /* not_found_is_ok */, XdrModuleData, callback);
}

void StoryStorageImpl::ReadAllModuleData(
    const AllModuleDataCallback& callback) {
  new ReadAllDataCall<ModuleData>(&operation_queue_, story_page_,
                                  kModuleKeyPrefix, XdrModuleData, callback);
}

void StoryStorageImpl::WriteModuleData(
    const fidl::Array<fidl::String>& module_path,
    const fidl::String& module_url,
    const LinkPathPtr& link_path,
    ModuleSource module_source,
    const SurfaceRelationPtr& surface_relation,
    bool module_stopped,
    const SyncCallback& callback) {
  ModuleDataPtr data = ModuleData::New();
  data->module_url = module_url;
  data->module_path = module_path.Clone();
  data->link_path = link_path.Clone();
  data->module_source = module_source;
  data->surface_relation = surface_relation.Clone();
  data->module_stopped = module_stopped;

  WriteModuleData(std::move(data), callback);
}

void StoryStorageImpl::WriteModuleData(ModuleDataPtr data,
                                       const SyncCallback& callback) {
  const std::string key{MakeModuleKey(data->module_path)};
  new WriteDataCall<ModuleData>(&operation_queue_, story_page_, key,
                                XdrModuleData, std::move(data), callback);
}

void StoryStorageImpl::WriteDeviceData(const std::string& story_id,
                                       const std::string& device_id,
                                       StoryState state,
                                       const SyncCallback& callback) {
  PerDeviceStoryInfoPtr data = PerDeviceStoryInfo::New();
  data->device_id = device_id;
  data->story_id = story_id;
  data->timestamp = time(nullptr);
  data->state = state;

  new WriteDataCall<PerDeviceStoryInfo, PerDeviceStoryInfoPtr>(
      &operation_queue_, story_page_, MakePerDeviceKey(device_id),
      XdrPerDeviceStoryInfo, std::move(data), callback);
}

void StoryStorageImpl::Log(StoryContextLogPtr log_entry) {
  new WriteDataCall<StoryContextLog>(
      &operation_queue_, story_page_,
      MakeStoryContextLogKey(log_entry->signal, log_entry->time),
      XdrStoryContextLog, std::move(log_entry), [] {});
}

void StoryStorageImpl::ReadLog(const LogCallback& callback) {
  new ReadAllDataCall<StoryContextLog>(&operation_queue_, story_page_,
                                       kStoryContextLogKeyPrefix,
                                       XdrStoryContextLog, callback);
}

void StoryStorageImpl::Sync(const SyncCallback& callback) {
  new SyncCall(&operation_queue_, callback);
}

void StoryStorageImpl::FlushWatchers(const SyncCallback& callback) {
  new FlushWatchersCall(&operation_queue_, story_page_, callback);
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

void StoryStorageImpl::OnPageChange(const std::string& key,
                                    const std::string& value) {
  for (auto& watcher_entry : watchers_) {
    if (key == watcher_entry.key) {
      watcher_entry.watcher(value);
    }
  }
}

}  // namespace modular
