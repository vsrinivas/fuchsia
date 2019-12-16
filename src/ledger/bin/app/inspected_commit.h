// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_COMMIT_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_COMMIT_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <memory>
#include <set>
#include <vector>

#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/inspected_container.h"
#include "src/ledger/bin/app/inspected_entry.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// Represents to Inspect a commit and manages representation to Inspect of entries according to the
// |inspect_deprecated::ChildrenManager| type.
class InspectedCommit final : public inspect_deprecated::ChildrenManager {
 public:
  explicit InspectedCommit(async_dispatcher_t* dispatcher, inspect_deprecated::Node node,
                           std::unique_ptr<const storage::Commit> commit, ExpiringToken token,
                           InspectablePage* inspectable_page);
  InspectedCommit(const InspectedCommit&) = delete;
  InspectedCommit& operator=(const InspectedCommit&) = delete;
  ~InspectedCommit() override;

  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

  fit::closure CreateDetacher();

 private:
  // inspect_deprecated::ChildrenManager
  void GetNames(fit::function<void(std::set<std::string>)> callback) override;
  void Attach(std::string name, fit::function<void(fit::closure)> callback) override;

  void CheckDiscardable();

  inspect_deprecated::Node node_;
  InspectablePage* inspectable_page_;
  std::unique_ptr<const storage::Commit> commit_;
  // TODO(nathaniel): It's weird that we have to maintain this token, keeping our "inspection
  // [request]" open for the lifetime of this object. But since we hang onto the |storage::Commit|
  // that complains (DCHECKS) if it outlives the PageStorageImpl from which it came, we do. This
  // should be tweaked to be in better shape.
  ExpiringToken token_;
  inspect_deprecated::Node parents_node_;
  std::vector<inspect_deprecated::Node> parents_;
  inspect_deprecated::Node entries_node_;
  fit::deferred_callback entries_children_manager_retainer_;
  callback::AutoCleanableMap<std::string, InspectedContainer<InspectedEntry>>
      inspected_entry_containers_;
  fit::closure on_discardable_;
  // TODO(nathaniel): Replace this integer with a weak_ptr-less-in-this-case TokenManager.
  int64_t ongoing_storage_accesses_;
  // TODO(nathaniel): Replace this integer with a weak_ptr-less-in-this-case TokenManager.
  int64_t outstanding_detachers_;

  // This object passes |this|-capturing callbacks to objects that it doesn't own
  // (|ActivePageManagerContainer| and |ActivePageManager|), so it needs a |WeakPtrFactory| to keep
  // its callbacks scoped to itself. Must be the last member.
  WeakPtrFactory<InspectedCommit> weak_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_COMMIT_H_
