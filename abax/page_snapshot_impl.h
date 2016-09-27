// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_PAGE_SNAPSHOT_IMPL_H_
#define APPS_LEDGER_ABAX_PAGE_SNAPSHOT_IMPL_H_

#include <map>

#include "apps/ledger/abax/page_impl.h"
#include "apps/ledger/abax/serialization.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

class PageImpl;

class PageSnapshotImpl : public PageSnapshot {
 public:
  PageSnapshotImpl(mojo::InterfaceRequest<PageSnapshot> request,
                   std::map<std::string, std::string>* db,
                   PageImpl* page,
                   Serialization* serialization);
  ~PageSnapshotImpl() override;

 private:
  // PageSnapshot:
  void GetAll(mojo::Array<uint8_t> key_prefix,
              const GetAllCallback& callback) override;

  void GetKeys(mojo::Array<uint8_t> key_prefix,
               const GetKeysCallback& callback) override;

  void Get(mojo::Array<uint8_t> key, const GetCallback& callback) override;

  void GetPartial(mojo::Array<uint8_t> key,
                  int64_t offset,
                  int64_t max_size,
                  const GetPartialCallback& callback) override;

  std::map<std::string, std::string> const db_;
  PageImpl* const page_;
  Serialization* serialization_;
  mojo::Binding<PageSnapshot> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageSnapshotImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_ABAX_LEDGER_SNAPSHOT_IMPL_H_
