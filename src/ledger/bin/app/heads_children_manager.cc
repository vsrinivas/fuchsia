// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/heads_children_manager.h"

#include <lib/callback/auto_cleanable.h>
#include <lib/callback/ensure_called.h>
#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <vector>

#include "src/ledger/bin/inspect/inspect.h"

namespace ledger {

HeadsChildrenManager::HeadsChildrenManager(inspect_deprecated::Node* heads_node,
                                           InspectablePage* inspectable_page)
    : heads_node_(heads_node), inspectable_page_(inspectable_page) {
  inspected_heads_.set_on_empty([this] { CheckEmpty(); });
}

HeadsChildrenManager::~HeadsChildrenManager() = default;

void HeadsChildrenManager::set_on_empty(fit::closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool HeadsChildrenManager::IsEmpty() { return inspected_heads_.empty(); }

void HeadsChildrenManager::CheckEmpty() {
  if (on_empty_callback_ && IsEmpty()) {
    on_empty_callback_();
  }
}

void HeadsChildrenManager::GetNames(fit::function<void(std::vector<std::string>)> callback) {
  fit::function<void(std::vector<std::string>)> call_ensured_callback =
      callback::EnsureCalled(std::move(callback), std::vector<std::string>());
  inspectable_page_->NewInspection(
      [callback = std::move(call_ensured_callback)](storage::Status status, ExpiringToken token,
                                                    ActivePageManager* active_inspectable_page) {
        if (status != storage::Status::OK) {
          // Inspect is prepared to receive incomplete information; there's not really anything
          // further for us to do than to log that the function failed.
          FXL_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
          callback({});
          return;
        }
        FXL_DCHECK(active_inspectable_page);
        std::vector<const storage::CommitId> heads;
        status = active_inspectable_page->GetHeads(&heads);
        if (status != storage::Status::OK) {
          // Inspect is prepared to receive incomplete information; there's not really anything
          // further for us to do than to log that the function failed.
          FXL_LOG(WARNING) << "GetHeads returned non-OK status: " << status;
          callback({});
          return;
        }
        std::vector<std::string> head_display_names;
        head_display_names.reserve(heads.size());
        for (const storage::CommitId& head : heads) {
          head_display_names.push_back(CommitIdToDisplayName(head));
        }
        callback(head_display_names);
      });
}

void HeadsChildrenManager::Attach(std::string name, fit::function<void(fit::closure)> callback) {
  storage::CommitId head;
  if (!CommitDisplayNameToCommitId(name, &head)) {
    FXL_LOG(WARNING) << "Inspect passed invalid head display name: " << name;
    callback({});
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
  auto emplacement = inspected_heads_.emplace(std::piecewise_construct, std::forward_as_tuple(head),
                                              std::forward_as_tuple(std::move(head_node)));
  callback(emplacement.first->second.CreateDetacher());
}
}  // namespace ledger
