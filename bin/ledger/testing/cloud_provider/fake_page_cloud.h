// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
#define PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_

#include "garnet/lib/callback/auto_cleanable.h"
#include <fuchsia/cpp/cloud_provider.h>
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"

namespace ledger {

class FakePageCloud : public cloud_provider::PageCloud {
 public:
  FakePageCloud(InjectNetworkError inject_network_error);
  ~FakePageCloud() override;

  void set_on_empty(fxl::Closure on_empty) { on_empty_ = std::move(on_empty); }

  void Bind(fidl::InterfaceRequest<cloud_provider::PageCloud> request);

 private:
  void SendPendingCommits();
  bool MustReturnError();

  // cloud_provider::PageCloud:
  void AddCommits(fidl::VectorPtr<cloud_provider::CommitPtr> commits,
                  const AddCommitsCallback& callback) override;
  void GetCommits(fidl::VectorPtr<uint8_t> min_position_token,
                  const GetCommitsCallback& callback) override;
  void AddObject(fidl::VectorPtr<uint8_t> id,
                 fsl::SizedVmoTransportPtr data,
                 const AddObjectCallback& callback) override;
  void GetObject(fidl::VectorPtr<uint8_t> id,
                 const GetObjectCallback& callback) override;
  void SetWatcher(
      fidl::VectorPtr<uint8_t> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      const SetWatcherCallback& callback) override;

  InjectNetworkError inject_network_error_;
  size_t remaining_errors_to_inject_;

  fidl::BindingSet<cloud_provider::PageCloud> bindings_;
  fxl::Closure on_empty_;

  fidl::VectorPtr<cloud_provider::CommitPtr> commits_;
  std::map<std::string, std::string> objects_;

  // Watchers set by the client.
  class WatcherContainer;
  callback::AutoCleanableSet<WatcherContainer> containers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageCloud);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
