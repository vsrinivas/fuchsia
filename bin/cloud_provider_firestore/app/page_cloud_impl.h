// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_
#define PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_

#include <memory>
#include <utility>

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace cloud_provider_firestore {

class PageCloudImpl : public cloud_provider::PageCloud {
 public:
  explicit PageCloudImpl(
      f1dl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~PageCloudImpl() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  // cloud_provider::PageCloud:
  void AddCommits(f1dl::Array<cloud_provider::CommitPtr> commits,
                  const AddCommitsCallback& callback) override;
  void GetCommits(f1dl::Array<uint8_t> min_position_token,
                  const GetCommitsCallback& callback) override;
  void AddObject(f1dl::Array<uint8_t> id,
                 fsl::SizedVmoTransportPtr data,
                 const AddObjectCallback& callback) override;
  void GetObject(f1dl::Array<uint8_t> id,
                 const GetObjectCallback& callback) override;
  void SetWatcher(
      f1dl::Array<uint8_t> min_position_token,
      f1dl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      const SetWatcherCallback& callback) override;

  f1dl::Binding<cloud_provider::PageCloud> binding_;
  fxl::Closure on_empty_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageCloudImpl);
};

}  // namespace cloud_provider_firestore

#endif  // PERIDOT_BIN_CLOUD_PROVIDER_FIRESTORE_APP_PAGE_CLOUD_IMPL_H_
