// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_HEADS_CHILDREN_MANAGER_H_
#define SRC_LEDGER_BIN_APP_HEADS_CHILDREN_MANAGER_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/inspected_head.h"
#include "src/ledger/bin/app/token_manager.h"
#include "src/ledger/lib/callback/auto_cleanable.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// An |inspect_deprecated::ChildrenManager| that exposes to Inspect the commit IDs of this page's
// heads.
class HeadsChildrenManager final : public inspect_deprecated::ChildrenManager {
 public:
  explicit HeadsChildrenManager(async_dispatcher_t* dispatcher,
                                inspect_deprecated::Node* heads_node,
                                InspectablePage* inspectable_page);
  HeadsChildrenManager(const HeadsChildrenManager&) = delete;
  HeadsChildrenManager& operator=(const HeadsChildrenManager&) = delete;
  ~HeadsChildrenManager() override;

  void SetOnDiscardable(fit::closure on_discardable);
  bool IsDiscardable() const;

 private:
  // inspect_deprecated::ChildrenManager
  void GetNames(fit::function<void(std::set<std::string>)> callback) override;
  void Attach(std::string name, fit::function<void(fit::closure)> callback) override;

  void CheckDiscardable();

  inspect_deprecated::Node* heads_node_;
  InspectablePage* inspectable_page_;
  fit::closure on_discardable_;
  AutoCleanableMap<storage::CommitId, InspectedHead> inspected_heads_;
  TokenManager token_manager_;
};
}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_HEADS_CHILDREN_MANAGER_H_
