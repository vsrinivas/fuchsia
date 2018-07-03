// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
#define PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"

namespace ledger {

class FakePageCloud : public cloud_provider::PageCloud {
 public:
  FakePageCloud(InjectNetworkError inject_network_error);
  ~FakePageCloud() override;

  void set_on_empty(fit::closure on_empty) { on_empty_ = std::move(on_empty); }

  void Bind(fidl::InterfaceRequest<cloud_provider::PageCloud> request);

 private:
  void SendPendingCommits();
  bool MustReturnError(uint64_t request_signature);

  // cloud_provider::PageCloud:
  void AddCommits(fidl::VectorPtr<cloud_provider::Commit> commits,
                  AddCommitsCallback callback) override;
  void GetCommits(std::unique_ptr<cloud_provider::Token> min_position_token,
                  GetCommitsCallback callback) override;
  void AddObject(fidl::VectorPtr<uint8_t> id, fuchsia::mem::Buffer data,
                 AddObjectCallback callback) override;
  void GetObject(fidl::VectorPtr<uint8_t> id,
                 GetObjectCallback callback) override;
  void SetWatcher(
      std::unique_ptr<cloud_provider::Token> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      SetWatcherCallback callback) override;

  InjectNetworkError inject_network_error_;
  std::map<uint64_t, size_t> remaining_errors_to_inject_;

  fidl::BindingSet<cloud_provider::PageCloud> bindings_;
  fit::closure on_empty_;

  fidl::VectorPtr<cloud_provider::Commit> commits_;
  std::map<std::string, std::string> objects_;

  // Watchers set by the client.
  class WatcherContainer;
  callback::AutoCleanableSet<WatcherContainer> containers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageCloud);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
