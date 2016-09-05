// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/page_impl.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "apps/ledger/abax/ledger_impl.h"
#include "apps/ledger/abax/page_connector.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {

namespace {

EntryChangePtr NewReferenceEntryChange(mojo::Array<uint8_t> key,
                                       ReferencePtr reference) {
  EntryChangePtr change = EntryChange::New();
  change->key = std::move(key);
  if (!reference.is_null()) {
    change->new_value = BytesOrReference::New();
    change->new_value->set_reference(std::move(reference));
  }
  return change;
}

EntryChangePtr NewValueEntryChange(mojo::Array<uint8_t> key,
                                   mojo::Array<uint8_t> value) {
  EntryChangePtr change = EntryChange::New();
  change->key = std::move(key);
  if (!value.is_null()) {
    change->new_value = BytesOrReference::New();
    change->new_value->set_bytes(std::move(value));
  }
  return change;
}

PageChangePtr NewPageChange(mojo::Array<EntryChangePtr> changes) {
  PageChangePtr change = PageChange::New();
  // TODO(nellyv): Update with actual timestamp once implemented.
  change->timestamp = 0;
  change->changes = std::move(changes);
  return change;
}

PageChangePtr NewSingleEntryPageChange(EntryChangePtr change) {
  mojo::Array<EntryChangePtr> array = mojo::Array<EntryChangePtr>::New(1);
  array[0] = std::move(change);
  return NewPageChange(std::move(array));
}

PageChangePtr NewSingleValuePageChange(mojo::Array<uint8_t> key,
                                       mojo::Array<uint8_t> value) {
  return NewSingleEntryPageChange(
      NewValueEntryChange(std::move(key), std::move(value)));
}

PageChangePtr NewSingleReferencePageChange(mojo::Array<uint8_t> key,
                                           ReferencePtr reference) {
  return NewSingleEntryPageChange(
      NewReferenceEntryChange(std::move(key), std::move(reference)));
}

StreamPtr ToStream(const std::string& value, int64_t offset, int64_t max_size) {
  size_t start = value.size();
  if (static_cast<size_t>(std::abs(offset)) < value.size()) {
    start = offset < 0 ? value.size() + offset : offset;
  }
  size_t length = max_size < 0 ? value.size() : max_size;
  std::string value_to_send = value.substr(start, length);
  StreamPtr streamed_value = Stream::New();
  streamed_value->size = value_to_send.size();
  streamed_value->data = mtl::WriteStringToConsumerHandle(value_to_send);
  return streamed_value;
}

}  // namespace

class PageImpl::DataPipeDrainerClient : public mtl::DataPipeDrainer::Client {
 public:
  DataPipeDrainerClient();
  ~DataPipeDrainerClient() override;

  void Start(mojo::ScopedDataPipeConsumerHandle source,
             const std::function<void(const std::string&)>& callback);

 private:
  // mtl::DataPipeDrainer::Client:
  void OnDataAvailable(const void* data, size_t num_bytes) override;
  void OnDataComplete() override;

  std::string content_;
  std::function<void(const std::string&)> callback_;
  mojo::ScopedDataPipeConsumerHandle source_;
  std::unique_ptr<mtl::DataPipeDrainer> drainer_;
};

PageImpl::DataPipeDrainerClient::DataPipeDrainerClient() {}

PageImpl::DataPipeDrainerClient::~DataPipeDrainerClient() {}

void PageImpl::DataPipeDrainerClient::Start(
    mojo::ScopedDataPipeConsumerHandle source,
    const std::function<void(const std::string&)>& callback) {
  callback_ = callback,
  drainer_.reset(new mtl::DataPipeDrainer(this, std::move(source)));
}

void PageImpl::DataPipeDrainerClient::OnDataAvailable(const void* data,
                                                      size_t num_bytes) {
  content_.append(static_cast<const char*>(data), num_bytes);
}

void PageImpl::DataPipeDrainerClient::OnDataComplete() {
  mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
      [this]() { callback_(content_); });
}

PageImpl::PageImpl(mojo::Array<uint8_t> id,
                   std::map<std::string, std::string>* db, LedgerImpl* ledger)
    : id_(std::move(id)),
      db_(db),
      ledger_(ledger),
      serialization_(id_),
      local_storage_(db, &serialization_) {}

PageImpl::~PageImpl() {}

bool PageImpl::Exists() {
  return db_->find(serialization_.MetaRowKey()) != db_->end();
}

Status PageImpl::Initialize() {
  (*db_)[serialization_.MetaRowKey()] = "";
  return Status::OK;
}

Status PageImpl::Delete() {
  std::string prefix = serialization_.PagePrefix();
  db_->erase(db_->lower_bound(prefix), serialization_.PrefixEnd(*db_, prefix));
  return Status::OK;
}

void PageImpl::AddConnector(mojo::InterfaceRequest<Page> request) {
  page_connectors_.push_back(std::unique_ptr<PageConnector>(
      new PageConnector(std::move(request), this)));
}

void PageImpl::OnConnectorError(PageConnector* connector) {
  page_connectors_.erase(
      std::remove_if(page_connectors_.begin(), page_connectors_.end(),
                     [connector](const std::unique_ptr<PageConnector>& c) {
                       return c.get() == connector;
                     }),
      page_connectors_.end());

  if (page_connectors_.empty()) {
    ledger_->OnPageError(id_);
  }
}

void PageImpl::OnSnapshotError(PageSnapshotImpl* snapshot) {
  snapshots_.erase(
      std::remove_if(snapshots_.begin(), snapshots_.end(),
                     [snapshot](const std::unique_ptr<PageSnapshotImpl>& s) {
                       return s.get() == snapshot;
                     }),
      snapshots_.end());
}

mojo::Array<uint8_t> PageImpl::GetId() { return id_.Clone(); }

PageSnapshotPtr PageImpl::GetSnapshot() {
  PageSnapshotPtr snapshot;
  auto snapshotImpl = std::unique_ptr<PageSnapshotImpl>(
      new PageSnapshotImpl(GetProxy(&snapshot), db_, this, &serialization_));
  snapshots_.push_back(std::move(snapshotImpl));
  return snapshot;
}

Status PageImpl::Watch(mojo::InterfaceHandle<PageWatcher> watcher) {
  PageWatcherPtr pageWatcher =
      mojo::InterfacePtr<PageWatcher>::Create(std::move(watcher));
  pageWatcher->OnInitialState(GetSnapshot(), []() {});
  PageWatcher* instance = pageWatcher.get();
  pageWatcher.set_connection_error_handler(
      [this, instance]() { OnWatcherError(instance); });
  watchers_.push_back(std::move(pageWatcher));
  return Status::OK;
}

Status PageImpl::Put(mojo::Array<uint8_t> key, mojo::Array<uint8_t> value,
                     ChangeSource source) {
  std::string value_row_key;
  if (!local_storage_.WriteEntryValue(value, &value_row_key) ||
      !local_storage_.WriteReference(key, value_row_key)) {
    return Status::UNKNOWN_ERROR;
  }

  UpdateWatchers(NewSingleValuePageChange(std::move(key), std::move(value)));
  return Status::OK;
}

// PutReference(array<uint8> key, Reference reference) => (Status status);
Status PageImpl::PutReference(mojo::Array<uint8_t> key,
                              ReferencePtr reference) {
  // Check that the reference exists.
  std::string reference_key_slice = convert::ToString(reference->opaque_id);
  if (db_->find(reference_key_slice) == db_->end()) {
    return Status::REFERENCE_NOT_FOUND;
  }

  if (!local_storage_.WriteReference(key, reference_key_slice)) {
    return Status::UNKNOWN_ERROR;
  }

  UpdateWatchers(
      NewSingleReferencePageChange(std::move(key), std::move(reference)));
  return Status::OK;
}

Status PageImpl::Delete(mojo::Array<uint8_t> key, ChangeSource source) {
  db_->erase(serialization_.GetReferenceRowKey(key));

  UpdateWatchers(
      NewSingleValuePageChange(std::move(key), mojo::Array<uint8_t>()));
  return Status::OK;
}

void PageImpl::CreateReference(
    int64_t size, mojo::ScopedDataPipeConsumerHandle data,
    const std::function<void(Status, ReferencePtr)>& callback) {
  std::unique_ptr<DataPipeDrainerClient> drainer(new DataPipeDrainerClient());
  DataPipeDrainerClient* drainer_ptr = drainer.get();
  drainers_.push_back(std::move(drainer));
  drainer_ptr->Start(std::move(data), [this, size, callback, drainer_ptr](
                                          const std::string& content) {
    OnReferenceDrainerComplete(size, std::move(callback), drainer_ptr, content);
  });
}

Status PageImpl::GetReferenceById(const convert::BytesReference& id,
                                  ValuePtr* value) {
  auto data = db_->find(convert::ToString(id));
  if (data == db_->end()) {
    return Status::REFERENCE_NOT_FOUND;
  }

  // TODO(qsr): Send a reference if the value is too large.
  *value = Value::New();
  (*value)->set_bytes(convert::ToArray(data->second));
  return Status::OK;
}

Status PageImpl::GetReference(ReferencePtr reference, ValuePtr* value) {
  return GetReferenceById(std::move(reference->opaque_id), value);
}

Status PageImpl::GetPartialReference(ReferencePtr reference, int64_t offset,
                                     int64_t max_size, StreamPtr* stream) {
  ValuePtr value;
  Status status = GetReference(std::move(reference), &value);

  if (status != Status::OK) {
    *stream = nullptr;
    return status;
  }

  FTL_DCHECK(!value->is_stream());
  *stream = ToStream(convert::ToString(value->get_bytes()), offset, max_size);
  return status;
}

void PageImpl::UpdateWatchers(const PageChangePtr& change) {
  for (const PageWatcherPtr& watcher : watchers_) {
    watcher->OnChange(change.Clone(), []() {});
  }
}

void PageImpl::OnWatcherError(PageWatcher* watcher) {
  watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(),
                                 [&watcher](const PageWatcherPtr& s) {
                                   return s.get() == watcher;
                                 }),
                  watchers_.end());
}

void PageImpl::OnReferenceDrainerComplete(
    int64_t size, const std::function<void(Status, ReferencePtr)>& callback,
    DataPipeDrainerClient* drainer, const std::string& content) {
  // Clear drainer when leaving this method.
  auto drainerIt =
      std::find_if(drainers_.begin(), drainers_.end(),
                   [drainer](const std::unique_ptr<DataPipeDrainerClient>& c) {
                     return c.get() == drainer;
                   });
  FTL_DCHECK(drainerIt != drainers_.end());
  std::unique_ptr<DataPipeDrainerClient> to_clean = std::move(*drainerIt);
  drainers_.erase(drainerIt);

  if (size >= 0 && content.size() != static_cast<uint64_t>(size)) {
    FTL_LOG(ERROR) << "Data read from data pipe is incomplete. Expected size: "
                   << size << ", but got: " << content.size();
    callback(Status::IO_ERROR, nullptr);
    return;
  }

  std::string value_row_key;
  if (!local_storage_.WriteEntryValue(content, &value_row_key)) {
    callback(Status::UNKNOWN_ERROR, nullptr);
    return;
  }

  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(value_row_key);
  callback(Status::OK, std::move(reference));
}

}  // namespace ledger
