// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/merging/custom_merge_strategy.h"

#include <lib/fit/function.h>

#include <memory>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
CustomMergeStrategy::CustomMergeStrategy(ConflictResolverPtr conflict_resolver)
    : conflict_resolver_(std::move(conflict_resolver)) {
  conflict_resolver_.set_error_handler([this](zx_status_t status) {
    // If a merge is in progress, it must be terminated.
    if (in_progress_merge_) {
      // The actual cleanup of in_progress_merge_ will happen in its on_done
      // callback.
      in_progress_merge_->Cancel();
    }
    if (on_error_) {
      // It is safe to call |on_error_| because the error handler waits for the
      // merges to finish before deleting this object.
      on_error_();
    }
  });
}

CustomMergeStrategy::~CustomMergeStrategy() = default;

void CustomMergeStrategy::SetOnError(fit::closure on_error) { on_error_ = std::move(on_error); }

void CustomMergeStrategy::Merge(storage::PageStorage* storage,
                                ActivePageManager* active_page_manager,
                                std::unique_ptr<const storage::Commit> head_1,
                                std::unique_ptr<const storage::Commit> head_2,
                                std::unique_ptr<const storage::Commit> ancestor,
                                fit::function<void(Status)> callback) {
  LEDGER_DCHECK(storage::Commit::TimestampOrdered(head_1, head_2));
  LEDGER_DCHECK(!in_progress_merge_);

  in_progress_merge_ = std::make_unique<ConflictResolverClient>(
      storage, active_page_manager, conflict_resolver_.get(), std::move(head_2), std::move(head_1),
      std::move(ancestor), [this, callback = std::move(callback)](Status status) {
        in_progress_merge_.reset();
        callback(status);
      });

  in_progress_merge_->Start();
}

void CustomMergeStrategy::Cancel() {
  if (in_progress_merge_) {
    in_progress_merge_->Cancel();
  }
}

}  // namespace ledger
