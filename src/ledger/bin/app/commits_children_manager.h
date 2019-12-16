// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_COMMITS_CHILDREN_MANAGER_H_
#define SRC_LEDGER_BIN_APP_COMMITS_CHILDREN_MANAGER_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>

#include <set>
#include <string>
#include <vector>

#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/inspected_commit.h"
#include "src/ledger/bin/app/inspected_container.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// An |inspect_deprecated::ChildrenManager| that exposes to Inspect the page's commits.
class CommitsChildrenManager final : public inspect_deprecated::ChildrenManager {
 public:
  explicit CommitsChildrenManager(async_dispatcher_t* dispatcher,
                                  inspect_deprecated::Node* commits_node,
                                  InspectablePage* inspectable_page);
  CommitsChildrenManager(const CommitsChildrenManager&) = delete;
  CommitsChildrenManager& operator=(const CommitsChildrenManager&) = delete;
  ~CommitsChildrenManager() override;

  void SetOnDiscardable(fit::closure on_discardable);
  bool IsDiscardable() const;

 private:
  // inspect_deprecated::ChildrenManager
  void GetNames(fit::function<void(std::set<std::string>)> callback) override;
  void Attach(std::string name, fit::function<void(fit::closure)> callback) override;

  void CheckDiscardable();

  async_dispatcher_t* dispatcher_;
  inspect_deprecated::Node* commits_node_;
  InspectablePage* inspectable_page_;
  fit::closure on_discardable_;
  callback::AutoCleanableMap<storage::CommitId, InspectedContainer<InspectedCommit>>
      inspected_commit_containers_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_COMMITS_CHILDREN_MANAGER_H_
