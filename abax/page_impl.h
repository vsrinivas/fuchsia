// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_PAGE_IMPL_H_
#define APPS_LEDGER_ABAX_PAGE_IMPL_H_

#include <map>
#include <memory>

#include "apps/ledger/abax/local_storage.h"
#include "apps/ledger/abax/page_snapshot_impl.h"
#include "apps/ledger/abax/serialization.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/convert/convert.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

class PageConnector;
class LedgerImpl;
class PageSnapshotImpl;

// Indicates the origin of a particular change.
enum class ChangeSource { LOCAL, SYNC };

// Accesses the database to read and write rows for a given Page.
//
// Each Page has an additional metadata row through which we verify whether a
// page exists or not.
class PageImpl {
 public:
  // Creates a new PageImpl with the given id.
  PageImpl(mojo::Array<uint8_t> id, std::map<std::string, std::string>* db,
           LedgerImpl* ledger);
  ~PageImpl();

  // Returns true if this Page exists.
  bool Exists();

  // Initializes the Page in the database. Calling |Exists()| after
  // initialization will return true.
  Status Initialize();

  // Deletes all the content of this Page. Upon successful deletion
  // Page::Exists() will return false. In case of failure,
  // Status::PAGE_NOT_FOUND is return if the Page does not exist, or
  // Status::UNKNOWN_ERROR in case of internal error.
  Status Delete();

  void AddConnector(mojo::InterfaceRequest<Page> request);

  void OnConnectorError(PageConnector* connector);

  void OnSnapshotError(PageSnapshotImpl* snapshot);

  mojo::Array<uint8_t> GetId();

  PageSnapshotPtr GetSnapshot();

  Status Watch(mojo::InterfaceHandle<PageWatcher> watcher);

  Status Put(mojo::Array<uint8_t> key, mojo::Array<uint8_t> value,
             ChangeSource source);

  Status PutReference(mojo::Array<uint8_t> key, ReferencePtr reference);

  Status Delete(mojo::Array<uint8_t> key, ChangeSource source);

  void CreateReference(
      int64_t size, mojo::ScopedDataPipeConsumerHandle data,
      const std::function<void(Status, ReferencePtr)>& callback);

  Status GetReferenceById(const convert::BytesReference& id, ValuePtr* value);

  Status GetReference(ReferencePtr reference, ValuePtr* value);

  Status GetPartialReference(ReferencePtr reference, int64_t offset,
                             int64_t max_size, StreamPtr* stream);

 private:
  class DataPipeDrainerClient;

  void UpdateWatchers(const PageChangePtr& change);

  void OnWatcherError(PageWatcher* watcher);

  void OnReferenceDrainerComplete(
      int64_t size, const std::function<void(Status, ReferencePtr)>& callback,
      DataPipeDrainerClient* drainer, const std::string& content);

  const mojo::Array<uint8_t> id_;
  std::map<std::string, std::string>* const db_;
  LedgerImpl* const ledger_;
  Serialization serialization_;
  LocalStorage local_storage_;
  std::vector<std::unique_ptr<PageSnapshotImpl>> snapshots_;
  std::vector<PageWatcherPtr> watchers_;

  std::vector<std::unique_ptr<PageConnector>> page_connectors_;
  std::vector<std::unique_ptr<DataPipeDrainerClient>> drainers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_ABAX_PAGE_IMPL_H_
