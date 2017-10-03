// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
#define PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_

#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace ledger {

class FakePageCloud : public cloud_provider::PageCloud {
 public:
  FakePageCloud(fidl::InterfaceRequest<cloud_provider::PageCloud> request);
  ~FakePageCloud() override;

  void set_on_empty(const fxl::Closure& on_empty) { on_empty_ = on_empty; }

 private:
  void SendPendingCommits();

  // cloud_provider::PageCloud:
  void AddCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                  const AddCommitsCallback& callback) override;
  void GetCommits(fidl::Array<uint8_t> min_position_token,
                  const GetCommitsCallback& callback) override;
  void AddObject(fidl::Array<uint8_t> id,
                 zx::vmo data,
                 const AddObjectCallback& callback) override;
  void GetObject(fidl::Array<uint8_t> id,
                 const GetObjectCallback& callback) override;
  void SetWatcher(
      fidl::Array<uint8_t> min_position_token,
      fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
      const SetWatcherCallback& callback) override;

  fidl::Binding<cloud_provider::PageCloud> binding_;
  fxl::Closure on_empty_;

  fidl::Array<cloud_provider::CommitPtr> commits_;
  std::map<std::string, std::string> objects_;

  // Watcher set by the client.
  cloud_provider::PageCloudWatcherPtr watcher_;

  // Whether we're still waiting for the watcher to ack the previous commit
  // notification.
  bool waiting_for_watcher_ack_ = false;

  // Index of the first commit to be sent to the watcher.
  size_t first_pending_commit_index_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageCloud);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TEST_CLOUD_PROVIDER_FAKE_PAGE_CLOUD_H_
