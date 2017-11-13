// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LtICENSE file.

#include "peridot/lib/ledger_client/ledger_client.h"

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/status.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

struct LedgerClient::PageEntry {
  LedgerPageId page_id;
  ledger::PagePtr page;
  std::vector<PageClient*> clients;
};

namespace {

void GetDiffRecursive(ledger::MergeResultProvider* const result,
                      const bool left,
                      ledger::PageChange* const change_all,
                      LedgerPageKey token,
                      std::function<void(ledger::Status)> callback) {
  auto cont = fxl::MakeCopyable(
      [result, left, change_all, callback = std::move(callback)](
          ledger::Status status, ledger::PageChangePtr change_delta,
          LedgerPageKey token) {
        if (status != ledger::Status::OK &&
            status != ledger::Status::PARTIAL_RESULT) {
          callback(status);
          return;
        }

        for (auto& entry : change_delta->changes) {
          change_all->changes.push_back(std::move(entry));
        }

        for (auto& deleted : change_delta->deleted_keys) {
          change_all->deleted_keys.push_back(std::move(deleted));
        }

        if (status == ledger::Status::OK) {
          callback(ledger::Status::OK);
          return;
        }

        GetDiffRecursive(result, left, change_all, std::move(token),
                         std::move(callback));
      });

  if (left) {
    result->GetLeftDiff(std::move(token), cont);
  } else {
    result->GetRightDiff(std::move(token), cont);
  }
}

void GetDiff(ledger::MergeResultProvider* const result,
             const bool left,
             ledger::PageChange* const change_all,
             std::function<void(ledger::Status)> callback) {
  GetDiffRecursive(result, left, change_all, nullptr /* token */,
                   std::move(callback));
}

bool HasPrefix(const std::string& value, const std::string& prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }

  for (size_t i = 0; i < prefix.size(); ++i) {
    if (value[i] != prefix[i]) {
      return false;
    }
  }

  return true;
}

}  // namespace

class LedgerClient::ConflictResolverImpl::ResolveCall : Operation<> {
 public:
  ResolveCall(OperationContainer* const container,
              ConflictResolverImpl* const impl,
              ledger::MergeResultProviderPtr result_provider,
              ledger::PageSnapshotPtr left_version,
              ledger::PageSnapshotPtr right_version,
              ledger::PageSnapshotPtr common_version)
      : Operation("LedgerClient::ResolveCall", container, [] {}),
        impl_(impl),
        result_provider_(std::move(result_provider)),
        left_version_(std::move(left_version)),
        right_version_(std::move(right_version)),
        common_version_(std::move(common_version)),
        change_left_(ledger::PageChange::New()),
        change_right_(ledger::PageChange::New()) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    GetDiff(result_provider_.get(), true /* left */, change_left_.get(),
            [this, flow](ledger::Status status) {
              has_left_ = true;
              Cont(flow);
            });

    GetDiff(result_provider_.get(), false /* left */, change_right_.get(),
            [this, flow](ledger::Status status) {
              has_right_ = true;
              Cont(flow);
            });

    GetEntries(
        left_version_.get(), &left_entries_,
        [this, flow](ledger::Status) { LogEntries("left", left_entries_); });

    GetEntries(
        right_version_.get(), &right_entries_,
        [this, flow](ledger::Status) { LogEntries("right", right_entries_); });

    GetEntries(common_version_.get(), &common_entries_,
               [this, flow](ledger::Status) {
                 LogEntries("common", common_entries_);
               });
  }

  void Cont(FlowToken flow) {
    if (!has_right_ || !has_left_) {
      return;
    }

    CollectConflicts(change_left_.get(), true /* left */);
    CollectConflicts(change_right_.get(), false /* left */);

    std::vector<PageClient*> page_clients;
    impl_->GetPageClients(&page_clients);

    for (auto& pair : conflicts_) {
      PageClient::Conflict* const conflict = pair.second.get();
      fidl::Array<ledger::MergedValuePtr> merge_changes;

      if (conflict->has_left && conflict->has_right) {
        for (PageClient* const page_client : page_clients) {
          if (HasPrefix(pair.first, page_client->prefix())) {
            page_client->OnPageConflict(conflict);

            if (pair.second->resolution != PageClient::LEFT) {
              ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
              merged_value->key = to_array(conflict->key);
              if (pair.second->resolution == PageClient::RIGHT) {
                merged_value->source = ledger::ValueSource::RIGHT;
              } else {
                if (pair.second->merged_is_deleted) {
                  merged_value->source = ledger::ValueSource::DELETE;
                } else {
                  merged_value->source = ledger::ValueSource::NEW;
                  merged_value->new_value = ledger::BytesOrReference::New();
                  merged_value->new_value->set_bytes(
                      to_array(pair.second->merged));
                }
              }
              merge_changes.push_back(std::move(merged_value));
            }

            // First client handles the conflict.
            //
            // TODO(mesch): We should order clients reverse-lexicographically by
            // prefix, so that longer prefixes are checked first.
            //
            // TODO(mesch): Default resolution could then be PASS, which would
            // pass to the next matching client. Too easy to abuse though.
            //
            // TODO(mesch): Best would be if overlapping prefixes are
            // prohibited.
            break;
          }
        }

      } else if (conflict->has_right) {
        ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
        merged_value->key = to_array(conflict->key);
        merged_value->source = ledger::ValueSource::RIGHT;
        merge_changes.push_back(std::move(merged_value));
      }

      if (merge_changes.size() > 0) {
        merge_count_++;
        result_provider_->Merge(
            std::move(merge_changes), [this, flow](ledger::Status status) {
              if (status != ledger::Status::OK) {
                FXL_LOG(ERROR) << "ResultProvider.Merge() " << status;
              }
              MergeDone(flow);
            });
      }
    }

    MergeDone(flow);
  }

  void MergeDone(FlowToken flow) {
    if (--merge_count_ == 0) {
      result_provider_->Done([this, flow](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "ResultProvider.Done() " << status;
        }
        impl_->NotifyWatchers();
      });
    }
  }

  void CollectConflicts(ledger::PageChange* const change, const bool left) {
    for (auto& entry : change->changes) {
      const std::string key = to_string(entry->key);

      if (!entry->value.is_valid()) {
        // TODO(mesch)
        continue;
      }

      std::string value;
      if (!fsl::StringFromVmo(entry->value, &value)) {
        continue;
      }

      if (!conflicts_[key]) {
        conflicts_[key].reset(new PageClient::Conflict);
        conflicts_[key]->key = key;
      }

      if (left) {
        conflicts_[key]->has_left = true;
        conflicts_[key]->left = value;
      } else {
        conflicts_[key]->has_right = true;
        conflicts_[key]->right = value;
      }
    }
  }

  void LogEntries(const std::string& headline,
                  const std::vector<ledger::EntryPtr>& entries) {
    FXL_VLOG(4) << "Entries " << headline;
    for (const ledger::EntryPtr& entry : entries) {
      FXL_VLOG(4) << " - " << to_string(entry->key);
    }
  }

  ConflictResolverImpl* const impl_;
  ledger::MergeResultProviderPtr result_provider_;

  ledger::PageSnapshotPtr left_version_;
  ledger::PageSnapshotPtr right_version_;
  ledger::PageSnapshotPtr common_version_;

  std::vector<ledger::EntryPtr> left_entries_;
  std::vector<ledger::EntryPtr> right_entries_;
  std::vector<ledger::EntryPtr> common_entries_;

  ledger::PageChangePtr change_left_;
  ledger::PageChangePtr change_right_;

  bool has_left_{};
  bool has_right_{};

  std::map<std::string, std::unique_ptr<PageClient::Conflict>> conflicts_;

  int merge_count_{1};  // One extra for the call at the end.

  FXL_DISALLOW_COPY_AND_ASSIGN(ResolveCall);
};

LedgerClient::LedgerClient(ledger::LedgerRepository* const ledger_repository,
                           const std::string& name,
                           std::function<void()> error)
    : ledger_name_(name) {
  ledger_repository->Duplicate(
      ledger_repository_.NewRequest(), [error](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository::Duplicate() failed: "
                         << LedgerStatusToString(status);
          error();
        }
      });

  // Open Ledger.
  ledger_repository->GetLedger(
      to_array(name), ledger_.NewRequest(), [error](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository.GetLedger() failed: "
                         << LedgerStatusToString(status);
          error();
        }
      });

  // This must be the first call after GetLedger, otherwise the Ledger
  // starts with one reconciliation strategy, then switches to another.
  ledger_->SetConflictResolverFactory(
      bindings_.AddBinding(this), [error](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "Ledger.SetConflictResolverFactory() failed: "
                         << LedgerStatusToString(status);
          error();
        }
      });
}

LedgerClient::LedgerClient(ledger::LedgerRepository* const ledger_repository,
                           const std::string& name)
    : ledger_name_(name) {
  ledger_repository->Duplicate(
      ledger_repository_.NewRequest(), [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository.Duplicate() failed: "
                         << LedgerStatusToString(status);

          // No further error reporting, as this is used only in tests.
        }
      });

  // Open Ledger.
  ledger_repository->GetLedger(
      to_array(name), ledger_.NewRequest(), [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository.GetLedger() failed: "
                         << LedgerStatusToString(status);

          // No further error reporting, as this is used only in tests.
        }
      });

  // Not registering a conflict resolver here.
}

LedgerClient::~LedgerClient() = default;

std::unique_ptr<LedgerClient> LedgerClient::GetLedgerClientPeer() {
  // NOTE(mesch): std::make_unique() requires the constructor to be public.
  std::unique_ptr<LedgerClient> ret;
  ret.reset(new LedgerClient(ledger_repository_.get(), ledger_name_));
  return ret;
}

ledger::Page* LedgerClient::GetPage(PageClient* const page_client,
                                    const std::string& context,
                                    const LedgerPageId& page_id) {
  auto i = std::find_if(pages_.begin(), pages_.end(), [&page_id](auto& entry) {
    return entry->page_id.Equals(page_id);
  });

  if (i != pages_.end()) {
    (*i)->clients.push_back(page_client);
    return (*i)->page.get();
  }

  ledger::PagePtr page;
  ledger_->GetPage(
      page_id.Clone(), page.NewRequest(), [context](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FXL_LOG(ERROR) << "Ledger.GetPage() " << context << " " << status;
        }
      });

  auto entry = std::make_unique<PageEntry>();
  entry->page_id = page_id.Clone();
  entry->clients.push_back(page_client);

  entry->page = std::move(page);
  entry->page.set_connection_error_handler([context] {
    // TODO(mesch): If this happens, larger things are wrong. This should
    // probably be signalled up, or at least must be signalled to the page
    // client.
    FXL_LOG(ERROR) << context << ": "
                   << "Page connection unexpectedly closed.";
  });

  pages_.emplace_back(std::move(entry));
  return pages_.back()->page.get();
}

void LedgerClient::DropPageClient(PageClient* const page_client) {
  for (auto i = pages_.begin(); i < pages_.end(); ++i) {
    std::vector<PageClient*>* const page_clients = &(*i)->clients;

    auto ii = std::remove_if(
        page_clients->begin(), page_clients->end(),
        [page_client](PageClient* item) { return item == page_client; });

    if (ii != page_clients->end()) {
      page_clients->erase(ii, page_clients->end());

      if (page_clients->empty()) {
        ClearConflictResolver((*i)->page_id);
        pages_.erase(i);
        break;  // Loop variable now invalid; must not continue loop.
      }
    }
  }
}

void LedgerClient::GetPolicy(LedgerPageId page_id,
                             const GetPolicyCallback& callback) {
  auto i = std::find_if(pages_.begin(), pages_.end(), [&page_id](auto& entry) {
    return entry->page_id.Equals(page_id);
  });

  // This is wrong if GetPolicy() is called for a page before its page client
  // has registered. Therefore, if an app keeps multiple connections to a page,
  // the ones kept by page clients must be created first.
  //
  // TODO(mesch): Maybe AUTOMATIC_WITH_FALLBACK should always be used anyway,
  // and the resolver should deal with conflicts on pages that don't have a page
  // client.
  if (i != pages_.end()) {
    callback(ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK);
    return;
  }

  callback(ledger::MergePolicy::LAST_ONE_WINS);
}

void LedgerClient::NewConflictResolver(
    LedgerPageId page_id,
    fidl::InterfaceRequest<ledger::ConflictResolver> request) {
  for (auto& i : resolvers_) {
    if (i->page_id().Equals(page_id)) {
      i->Connect(std::move(request));
      return;
    }
  }

  resolvers_.emplace_back(
      std::make_unique<ConflictResolverImpl>(this, std::move(page_id)));
  resolvers_.back()->Connect(std::move(request));
}

void LedgerClient::ClearConflictResolver(const LedgerPageId& page_id) {
  auto i = std::remove_if(
      resolvers_.begin(), resolvers_.end(),
      [&page_id](auto& item) { return item->page_id().Equals(page_id); });

  if (i != resolvers_.end()) {
    resolvers_.erase(i, resolvers_.end());
  }
}

LedgerClient::ConflictResolverImpl::ConflictResolverImpl(
    LedgerClient* const ledger_client,
    LedgerPageId page_id)
    : ledger_client_(ledger_client), page_id_(std::move(page_id)) {}

LedgerClient::ConflictResolverImpl::~ConflictResolverImpl() = default;

void LedgerClient::ConflictResolverImpl::Connect(
    fidl::InterfaceRequest<ledger::ConflictResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

void LedgerClient::ConflictResolverImpl::Resolve(
    fidl::InterfaceHandle<ledger::PageSnapshot> left_version,
    fidl::InterfaceHandle<ledger::PageSnapshot> right_version,
    fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
    fidl::InterfaceHandle<ledger::MergeResultProvider> result_provider) {
  new ResolveCall(
      &operation_queue_, this,
      ledger::MergeResultProviderPtr::Create(std::move(result_provider)),
      ledger::PageSnapshotPtr::Create(std::move(left_version)),
      ledger::PageSnapshotPtr::Create(std::move(right_version)),
      ledger::PageSnapshotPtr::Create(std::move(common_version)));
}

void LedgerClient::ConflictResolverImpl::GetPageClients(
    std::vector<PageClient*>* page_clients) {
  auto i = std::find_if(
      ledger_client_->pages_.begin(), ledger_client_->pages_.end(),
      [this](auto& item) { return item->page_id.Equals(page_id_); });

  FXL_CHECK(i != ledger_client_->pages_.end());
  *page_clients = (*i)->clients;
}

void LedgerClient::ConflictResolverImpl::NotifyWatchers() const {
  for (auto& watcher : ledger_client_->watchers_) {
    watcher();
  }
}

}  // namespace modular
