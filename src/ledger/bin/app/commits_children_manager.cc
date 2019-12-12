// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/commits_children_manager.h"

#include <lib/fit/function.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/callback/ensure_called.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

CommitsChildrenManager::CommitsChildrenManager(async_dispatcher_t* dispatcher,
                                               inspect_deprecated::Node* commits_node,
                                               InspectablePage* inspectable_page)
    : dispatcher_(dispatcher),
      commits_node_(commits_node),
      inspectable_page_(inspectable_page),
      inspected_commit_containers_(dispatcher) {
  inspected_commit_containers_.SetOnDiscardable([this] { CheckDiscardable(); });
}

CommitsChildrenManager::~CommitsChildrenManager() = default;

void CommitsChildrenManager::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool CommitsChildrenManager::IsDiscardable() const { return inspected_commit_containers_.empty(); }

void CommitsChildrenManager::GetNames(fit::function<void(std::set<std::string>)> callback) {
  fit::function<void(std::set<std::string>)> call_ensured_callback =
      EnsureCalled(std::move(callback), std::set<std::string>());
  inspectable_page_->NewInspection([callback = std::move(call_ensured_callback)](
                                       storage::Status status, ExpiringToken token,
                                       ActivePageManager* active_page_manager) mutable {
    if (status != storage::Status::OK) {
      // Inspect is prepared to receive incomplete information; there's not really anything
      // further for us to do than to log that the function failed.
      LEDGER_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
      callback(std::set<std::string>());
      return;
    }
    LEDGER_DCHECK(active_page_manager);
    active_page_manager->GetCommits(
        [callback = std::move(callback), token = std::move(token)](
            Status status, const std::vector<std::unique_ptr<const storage::Commit>>& commits) {
          if (status != storage::Status::OK) {
            // Inspect is prepared to receive incomplete information; there's not really anything
            // further for us to do than to log that the function failed.
            LEDGER_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
            callback(std::set<std::string>());
            return;
          }
          std::set<std::string> commit_display_names;
          for (const std::unique_ptr<const storage::Commit>& commit : commits) {
            commit_display_names.insert(CommitIdToDisplayName(commit->GetId()));
          }
          callback(std::move(commit_display_names));
        });
  });
}

void CommitsChildrenManager::Attach(std::string name, fit::function<void(fit::closure)> callback) {
  storage::CommitId commit_id;
  if (!CommitDisplayNameToCommitId(name, &commit_id)) {
    LEDGER_LOG(WARNING) << "Inspect passed invalid commit display name: " << name;
    callback([] {});
    return;
  }

  if (auto it = inspected_commit_containers_.find(commit_id);
      it != inspected_commit_containers_.end()) {
    it->second.AddCallback(EnsureCalled(std::move(callback), fit::closure()));
    return;
  }
  auto emplacement = inspected_commit_containers_.try_emplace(
      commit_id, EnsureCalled(std::move(callback), fit::closure()));
  inspectable_page_->NewInspection([this, commit_display_name = std::move(name),
                                    commit_id = std::move(commit_id),
                                    emplacement = std::move(emplacement)](
                                       storage::Status status, ExpiringToken token,
                                       ActivePageManager* active_page_manager) mutable {
    if (status != storage::Status::OK) {
      // Inspect is prepared to receive incomplete information; there's not really anything
      // further for us to do than to log that the function failed.
      LEDGER_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
      emplacement.first->second.Abandon();
      return;
    }
    LEDGER_DCHECK(active_page_manager);
    active_page_manager->GetCommit(
        commit_id, [this, commit_display_name = std::move(commit_display_name),
                    emplacement = std::move(emplacement), token = std::move(token)](
                       Status status, std::unique_ptr<const storage::Commit> commit) mutable {
          // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=35416): Log a message in
          // the "real error; not just a garbage-collected commit" circumstance.
          if (status != Status::OK || !commit) {
            // NOTE(nathaniel): It's unexpected that Inspect would call us to attach a commit
            // that doesn't exist because all the commits about which Inspect knows are the ones
            // the IDs of which we reported to in in a call to GetNames.
            //
            // Maybe the commit was garbage-collected between having been reported to Inspect as
            // existing and Inspect having called to attach it?
            emplacement.first->second.Abandon();
            return;
          }
          inspect_deprecated::Node commit_node = commits_node_->CreateChild(commit_display_name);
          storage::CommitId commit_id = commit->GetId();
          emplacement.first->second.Mature(dispatcher_, std::move(commit_node), std::move(commit),
                                           std::move(token), inspectable_page_);
        });
  });
}

void CommitsChildrenManager::CheckDiscardable() {
  if (on_discardable_ && IsDiscardable()) {
    on_discardable_();
  }
}

}  // namespace ledger
