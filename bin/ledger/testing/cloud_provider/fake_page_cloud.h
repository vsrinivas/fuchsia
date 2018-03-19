// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
#define PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_

#include "garnet/lib/callback/auto_cleanable.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace ledger {

class FakePageCloud : public cloud_provider::PageCloud {
 public:
  FakePageCloud();
  ~FakePageCloud() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

  void Bind(f1dl::InterfaceRequest<cloud_provider::PageCloud> request);

 private:
  void SendPendingCommits();

  // cloud_provider::PageCloud:
  void AddCommits(f1dl::VectorPtr<cloud_provider::CommitPtr> commits,
                  const AddCommitsCallback& callback) override;
  void GetCommits(f1dl::VectorPtr<uint8_t> min_position_token,
                  const GetCommitsCallback& callback) override;
  void AddObject(f1dl::VectorPtr<uint8_t> id,
                 fsl::SizedVmoTransportPtr data,
                 const AddObjectCallback& callback) override;
  void GetObject(f1dl::VectorPtr<uint8_t> id,
                 const GetObjectCallback& callback) override;
  void SetWatcher(
      f1dl::VectorPtr<uint8_t> min_position_token,
      f1dl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      const SetWatcherCallback& callback) override;

  f1dl::BindingSet<cloud_provider::PageCloud> bindings_;
  fxl::Closure on_empty_;

  f1dl::VectorPtr<cloud_provider::CommitPtr> commits_;
  std::map<std::string, std::string> objects_;

  // Watchers set by the client.
  class WatcherContainer;
  callback::AutoCleanableSet<WatcherContainer> containers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageCloud);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
