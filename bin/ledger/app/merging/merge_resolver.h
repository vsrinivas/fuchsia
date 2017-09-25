// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_RESOLVER_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_RESOLVER_H_

#include <vector>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/backoff/backoff.h"
#include "peridot/bin/ledger/callback/scoped_task_runner.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace ledger {
class PageManager;
class MergeStrategy;

// MergeResolver watches a page and resolves conflicts as they appear using the
// provided merge strategy.
class MergeResolver : public storage::CommitWatcher {
 public:
  MergeResolver(fxl::Closure on_destroyed,
                Environment* environment,
                storage::PageStorage* storage,
                std::unique_ptr<backoff::Backoff> backoff);
  ~MergeResolver() override;

  void set_on_empty(fxl::Closure on_empty_callback);

  // Returns true if no merge is currently in progress.
  bool IsEmpty();

  // Changes the current merge strategy. Any pending merge will be cancelled.
  void SetMergeStrategy(std::unique_ptr<MergeStrategy> strategy);

  void SetPageManager(PageManager* page_manager);

 private:
  enum class DelayedStatus {
    INITIAL,
    DELAYED,
  };

  // storage::CommitWatcher:
  void OnNewCommits(
      const std::vector<std::unique_ptr<const storage::Commit>>& commits,
      storage::ChangeSource source) override;

  void PostCheckConflicts();
  void CheckConflicts(DelayedStatus delayed_status);
  void ResolveConflicts(DelayedStatus delayed_status,
                        std::vector<storage::CommitId> heads);

  Environment* const environment_;
  storage::PageStorage* const storage_;
  std::unique_ptr<backoff::Backoff> backoff_;
  PageManager* page_manager_ = nullptr;
  std::unique_ptr<MergeStrategy> strategy_;
  std::unique_ptr<MergeStrategy> next_strategy_;
  bool has_next_strategy_ = false;
  bool merge_in_progress_ = false;
  fxl::Closure on_empty_callback_;
  fxl::Closure on_destroyed_;

  // ScopedTaskRunner must be the last member of the class.
  callback::ScopedTaskRunner task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MergeResolver);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_MERGE_RESOLVER_H_
