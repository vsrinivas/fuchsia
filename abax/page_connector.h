// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_PAGE_CONNECTOR_H_
#define APPS_LEDGER_ABAX_PAGE_CONNECTOR_H_

#include "apps/ledger/abax/page_impl.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

// An implementation of the |Page| interface.
//
// |PageConnector| uses a |PageImpl| object to provide an implementation of its
// methods. While a new |PageConnector| is returned per page request through the
// Ledger API, only one |PageImpl| is instantiated per unique page requested.
// This way it is possible for the unique |PageImpl| to keep track of all open
// connections for a page and close them all together when necessary, like for
// example if a page is deleted.
class PageConnector : public Page {
 public:
  PageConnector(mojo::InterfaceRequest<Page> request, PageImpl* page);
  ~PageConnector() override;

 private:
  // Page:
  void GetId(const GetIdCallback& callback) override;

  void GetSnapshot(const GetSnapshotCallback& callback) override;

  void Watch(mojo::InterfaceHandle<PageWatcher> watcher,
             const WatchCallback& callback) override;

  void Put(mojo::Array<uint8_t> key,
           mojo::Array<uint8_t> value,
           const PutCallback& callback) override;

  void PutWithPriority(mojo::Array<uint8_t> key,
                       mojo::Array<uint8_t> value,
                       Priority priority,
                       const PutWithPriorityCallback& callback) override;

  void PutReference(mojo::Array<uint8_t> key,
                    ReferencePtr reference,
                    Priority priority,
                    const PutReferenceCallback& callback) override;

  void Delete(mojo::Array<uint8_t> key,
              const DeleteCallback& callback) override;

  void CreateReference(int64_t size,
                       mojo::ScopedDataPipeConsumerHandle data,
                       const CreateReferenceCallback& callback) override;

  void StartTransaction(const StartTransactionCallback& callback) override;

  void Commit(const CommitCallback& callback) override;

  void Rollback(const RollbackCallback& callback) override;

  PageImpl* const page_;

  mojo::Binding<Page> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageConnector);
};

}  // namespace ledger

#endif  // APPS_LEDGER_ABAX_PAGE_CONNECTOR_H_
