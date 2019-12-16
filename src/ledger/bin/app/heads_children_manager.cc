// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/heads_children_manager.h"

#include <lib/fit/function.h>

#include <set>
#include <vector>

#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/lib/callback/auto_cleanable.h"
#include "src/ledger/lib/callback/ensure_called.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

HeadsChildrenManager::HeadsChildrenManager(async_dispatcher_t* dispatcher,
                                           inspect_deprecated::Node* heads_node,
                                           InspectablePage* inspectable_page)
    : heads_node_(heads_node), inspectable_page_(inspectable_page), inspected_heads_(dispatcher) {
  token_manager_.SetOnDiscardable([this] { CheckDiscardable(); });
  inspected_heads_.SetOnDiscardable([this] { CheckDiscardable(); });
}

HeadsChildrenManager::~HeadsChildrenManager() = default;

void HeadsChildrenManager::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool HeadsChildrenManager::IsDiscardable() const {
  return token_manager_.IsDiscardable() && inspected_heads_.IsDiscardable();
}

void HeadsChildrenManager::CheckDiscardable() {
  if (on_discardable_ && IsDiscardable()) {
    on_discardable_();
  }
}

void HeadsChildrenManager::GetNames(fit::function<void(std::set<std::string>)> callback) {
  fit::function<void(std::set<std::string>)> call_ensured_callback =
      EnsureCalled(std::move(callback), std::set<std::string>());
  ExpiringToken token = token_manager_.CreateToken();
  inspectable_page_->NewInspection(
      [heads_children_manager_token = std::move(token),
       callback = std::move(call_ensured_callback)](storage::Status status, ExpiringToken token,
                                                    ActivePageManager* active_inspectable_page) {
        if (status != storage::Status::OK) {
          // Inspect is prepared to receive incomplete information; there's not really anything
          // further for us to do than to log that the function failed.
          LEDGER_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
          callback(std::set<std::string>{});
          return;
        }
        LEDGER_DCHECK(active_inspectable_page);
        std::vector<const storage::CommitId> heads;
        status = active_inspectable_page->GetHeads(&heads);
        if (status != storage::Status::OK) {
          // Inspect is prepared to receive incomplete information; there's not really anything
          // further for us to do than to log that the function failed.
          LEDGER_LOG(WARNING) << "GetHeads returned non-OK status: " << status;
          callback(std::set<std::string>{});
          return;
        }
        std::set<std::string> head_display_names;
        for (const storage::CommitId& head : heads) {
          head_display_names.insert(CommitIdToDisplayName(head));
        }
        callback(head_display_names);
      });
}

void HeadsChildrenManager::Attach(std::string name, fit::function<void(fit::closure)> callback) {
  storage::CommitId head;
  if (!CommitDisplayNameToCommitId(name, &head)) {
    LEDGER_LOG(WARNING) << "Inspect passed invalid head display name: " << name;
    callback([] {});
    return;
  }
  auto it = inspected_heads_.find(head);
  if (it != inspected_heads_.end()) {
    callback(it->second.CreateDetacher());
    return;
  }
  // We don't bother with a storage read because the head's name was originally found in a
  // call to |GetHeads| and there's nothing about the |inspect_deprecated::Node|
  // representing the head that would require another storage read. As for the possibility
  // that the page's heads may have changed between calls to |GetHeads| and
  // |AttachHead|: that race is inherent; the page's heads can just as easily change
  // immediately after any storage read performed at this point in the code.
  inspect_deprecated::Node head_node = heads_node_->CreateChild(name);
  auto emplacement = inspected_heads_.try_emplace(head, std::move(head_node));
  callback(emplacement.first->second.CreateDetacher());
}
}  // namespace ledger
