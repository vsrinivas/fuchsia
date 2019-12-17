// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_PAGE_CLOUD_H_
#define SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_PAGE_CLOUD_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/cloud_provider_in_memory/lib/diff_tree.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/ledger/lib/callback/auto_cleanable.h"
#include "src/ledger/lib/rng/random.h"

namespace ledger {

struct CommitRecord {
  std::string id;
  std::string data;
};

class FakePageCloud : public cloud_provider::PageCloud {
 public:
  explicit FakePageCloud(async_dispatcher_t* dispatcher, Random* random,
                         InjectNetworkError inject_network_error,
                         InjectMissingDiff inject_missing_diff);
  FakePageCloud(const FakePageCloud&) = delete;
  FakePageCloud& operator=(const FakePageCloud&) = delete;
  ~FakePageCloud() override;

  bool IsDiscardable() const;

  void SetOnDiscardable(fit::closure on_discardable);

  void Bind(fidl::InterfaceRequest<cloud_provider::PageCloud> request);

 private:
  void SendPendingCommits();
  bool MustReturnError(uint64_t request_signature);

  // cloud_provider::PageCloud:
  void AddCommits(cloud_provider::CommitPack commit_pack, AddCommitsCallback callback) override;
  void GetCommits(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                  GetCommitsCallback callback) override;
  void AddObject(std::vector<uint8_t> id, fuchsia::mem::Buffer data,
                 cloud_provider::ReferencePack references, AddObjectCallback callback) override;
  void GetObject(std::vector<uint8_t> id, GetObjectCallback callback) override;
  void SetWatcher(std::unique_ptr<cloud_provider::PositionToken> min_position_token,
                  fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> watcher,
                  SetWatcherCallback callback) override;
  void GetDiff(std::vector<uint8_t> commit_id, std::vector<std::vector<uint8_t>> possible_bases,
               GetDiffCallback callback) override;
  void UpdateClock(cloud_provider::ClockPack clock, UpdateClockCallback callback) override;

  Random* const random_;
  InjectNetworkError inject_network_error_;
  std::map<uint64_t, size_t> remaining_errors_to_inject_;
  InjectMissingDiff inject_missing_diff_;

  fidl::BindingSet<cloud_provider::PageCloud> bindings_;
  fit::closure on_discardable_;

  // The id and data of commits received until know, ordered.
  std::vector<CommitRecord> commits_;
  // The set of ids of the commits present in |commits_|.
  std::set<std::string> known_commits_;
  // The set of known diffs.
  DiffTree diffs_;
  std::map<std::string, std::string> objects_;

  // Watchers set by the client.
  class WatcherContainer;
  AutoCleanableSet<WatcherContainer> containers_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_CLOUD_PROVIDER_IN_MEMORY_LIB_FAKE_PAGE_CLOUD_H_
