// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_commit.h"

#include <lib/fit/function.h>

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "src/ledger/bin/app/inspectable_page.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/ensure_called.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

InspectedCommit::InspectedCommit(async_dispatcher_t* dispatcher, inspect_deprecated::Node node,
                                 std::unique_ptr<const storage::Commit> commit, ExpiringToken token,
                                 InspectablePage* inspectable_page)
    : node_(std::move(node)),
      inspectable_page_(inspectable_page),
      commit_(std::move(commit)),
      token_(std::move(token)),
      parents_node_(node_.CreateChild(convert::ToString(kParentsInspectPathComponent))),
      entries_node_(node_.CreateChild(convert::ToString(kEntriesInspectPathComponent))),
      entries_children_manager_retainer_(entries_node_.SetChildrenManager(this)),
      inspected_entry_containers_(dispatcher),
      ongoing_storage_accesses_(0),
      outstanding_detachers_(0),
      weak_factory_(this) {
  for (const storage::CommitIdView& parent_id : commit_->GetParentIds()) {
    parents_.emplace_back(
        parents_node_.CreateChild(CommitIdToDisplayName(convert::ToString(parent_id))));
  }
  inspected_entry_containers_.SetOnDiscardable([this] { CheckDiscardable(); });
}

InspectedCommit::~InspectedCommit() = default;

void InspectedCommit::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool InspectedCommit::IsDiscardable() const {
  return outstanding_detachers_ == 0 && ongoing_storage_accesses_ == 0 &&
         inspected_entry_containers_.empty();
}

fit::closure InspectedCommit::CreateDetacher() {
  outstanding_detachers_++;
  return [this] {
    outstanding_detachers_--;
    CheckDiscardable();
  };
}

void InspectedCommit::GetNames(fit::function<void(std::set<std::string>)> callback) {
  fit::function<void(std::set<std::string>)> call_ensured_callback =
      callback::EnsureCalled(std::move(callback), std::set<std::string>());
  ongoing_storage_accesses_++;
  inspectable_page_->NewInspection(callback::MakeScoped(
      weak_factory_.GetWeakPtr(),
      [this, callback = std::move(call_ensured_callback)](
          Status status, ExpiringToken token, ActivePageManager* active_page_manager) mutable {
        if (status != storage::Status::OK) {
          // Inspect is prepared to receive incomplete information; there's not really anything
          // further for us to do than to log that the function failed.
          LEDGER_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
          callback({});
          ongoing_storage_accesses_--;
          CheckDiscardable();
          return;
        }
        LEDGER_DCHECK(active_page_manager);

        std::unique_ptr<std::set<std::string>> key_display_names =
            std::make_unique<std::set<std::string>>();
        fit::function<bool(storage::Entry)> on_next =
            [key_display_names = key_display_names.get()](const storage::Entry& entry) {
              key_display_names->insert(KeyToDisplayName(entry.key));
              return true;
            };
        fit::function<void(storage::Status)> on_done =
            [this, callback = std::move(callback), key_display_names = std::move(key_display_names),
             token = std::move(token)](storage::Status status) mutable {
              if (status != storage::Status::OK) {
                // Inspect is prepared to receive incomplete information; there's not really
                // anything further for us to do than to log that the function failed.
                LEDGER_LOG(WARNING) << "GetEntries called back with non-OK status: " << status;
                callback(std::set<std::string>());
              } else {
                callback(std::move(*key_display_names));
              }
              ongoing_storage_accesses_--;
              CheckDiscardable();
            };
        active_page_manager->GetEntries(
            *commit_, "", std::move(on_next),
            callback::MakeScoped(weak_factory_.GetWeakPtr(), std::move(on_done)));
      }));
}

void InspectedCommit::Attach(std::string name, fit::function<void(fit::closure)> callback) {
  std::string key;
  if (!KeyDisplayNameToKey(name, &key)) {
    LEDGER_LOG(WARNING) << "Inspect passed invalid key display name: " << name;
    callback([] {});
    return;
  }

  auto it = inspected_entry_containers_.find(key);
  if (it != inspected_entry_containers_.end()) {
    it->second.AddCallback(callback::EnsureCalled(std::move(callback), fit::closure([] {})));
    return;
  }
  auto emplacement = inspected_entry_containers_.try_emplace(
      key, callback::EnsureCalled(std::move(callback), fit::closure([] {})));
  ongoing_storage_accesses_++;
  fxl::WeakPtr<InspectedCommit> weak_this = weak_factory_.GetWeakPtr();
  inspectable_page_->NewInspection(callback::MakeScoped(
      weak_this,
      [this, name = std::move(name), key = std::move(key), emplacement = std::move(emplacement),
       weak_this](Status status, ExpiringToken token,
                  ActivePageManager* active_page_manager) mutable {
        if (status != storage::Status::OK) {
          ongoing_storage_accesses_--;
          // Inspect is prepared to receive incomplete information; there's not really anything
          // further for us to do than to log that the function failed.
          LEDGER_LOG(WARNING) << "NewInternalRequest called back with non-OK status: " << status;
          emplacement.first->second.Abandon();
          return;
        }
        LEDGER_DCHECK(active_page_manager);
        active_page_manager->GetValue(
            *commit_, key,
            callback::MakeScoped(
                weak_this, [this, name = std::move(name), emplacement = std::move(emplacement),
                            token = std::move(token)](Status status, std::vector<uint8_t> value) {
                  ongoing_storage_accesses_--;
                  if (status != storage::Status::OK) {
                    // Inspect is prepared to receive incomplete information; there's not really
                    // anything further for us to do than to log that the function failed.
                    LEDGER_LOG(WARNING) << "GetValue called back with non-OK status: " << status;
                    emplacement.first->second.Abandon();
                    return;
                  }
                  inspect_deprecated::Node node = entries_node_.CreateChild(name);
                  emplacement.first->second.Mature(std::move(node), std::move(value));
                }));
      }));
}

void InspectedCommit::CheckDiscardable() {
  if (on_discardable_ && IsDiscardable()) {
    on_discardable_();
  }
}

}  // namespace ledger
