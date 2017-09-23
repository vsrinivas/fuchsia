// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/story_storage_impl.h"

#include "lib/fsl/vmo/strings.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/story/fidl/story_data.fidl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/storage.h"

namespace modular {

namespace {

// HACK(mesch): PageClient here is not used for watching the page, only to write
// to it. This will change soon.
constexpr char kNoPrefix[] = "=======";

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
  xdr->Field("surface_relation", &data->surface_relation, XdrSurfaceRelation);
  xdr->Field("module_stopped", &data->module_stopped);
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

StoryStorageImpl::StoryStorageImpl(LedgerClient* const ledger_client,
                                   LedgerPageId story_page_id)
    : PageClient("StoryStorageImpl", ledger_client, std::move(story_page_id),
                 kNoPrefix) {}

StoryStorageImpl::~StoryStorageImpl() = default;

void StoryStorageImpl::ReadModuleData(
    const fidl::Array<fidl::String>& module_path,
    const ModuleDataCallback& callback) {
  new ReadDataCall<ModuleData>(
      &operation_queue_, page(), MakeModuleKey(module_path),
      false /* not_found_is_ok */, XdrModuleData, callback);
}

void StoryStorageImpl::ReadAllModuleData(
    const AllModuleDataCallback& callback) {
  new ReadAllDataCall<ModuleData>(&operation_queue_, page(), kModuleKeyPrefix,
                                  XdrModuleData, callback);
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
  new WriteDataCall<ModuleData>(&operation_queue_, page(), key, XdrModuleData,
                                std::move(data), callback);
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
      &operation_queue_, page(), MakePerDeviceKey(device_id),
      XdrPerDeviceStoryInfo, std::move(data), callback);
}

void StoryStorageImpl::Log(StoryContextLogPtr log_entry) {
  new WriteDataCall<StoryContextLog>(
      &operation_queue_, page(),
      MakeStoryContextLogKey(log_entry->signal, log_entry->time),
      XdrStoryContextLog, std::move(log_entry), [] {});
}

void StoryStorageImpl::ReadLog(const LogCallback& callback) {
  new ReadAllDataCall<StoryContextLog>(&operation_queue_, page(),
                                       kStoryContextLogKeyPrefix,
                                       XdrStoryContextLog, callback);
}

void StoryStorageImpl::Sync(const SyncCallback& callback) {
  new SyncCall(&operation_queue_, callback);
}

}  // namespace modular
