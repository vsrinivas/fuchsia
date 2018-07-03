// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LtICENSE file.

#include "peridot/lib/ledger_client/ledger_client.h"

#include <algorithm>

#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/status.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

struct LedgerClient::PageEntry {
  LedgerPageId page_id;
  fuchsia::ledger::PagePtr page;
  std::vector<PageClient*> clients;
};

namespace {

PageClient::Conflict ToConflict(const fuchsia::ledger::DiffEntry* entry) {
  PageClient::Conflict conflict;
  conflict.key = entry->key.Clone();
  if (entry->left) {
    conflict.has_left = true;
    std::string value;
    if (!fsl::StringFromVmo(*entry->left->value, &value)) {
      FXL_LOG(ERROR) << "Unable to read vmo for left entry of "
                     << to_hex_string(conflict.key) << ".";
      return conflict;
    }
    conflict.left = std::move(value);
  } else {
    conflict.left_is_deleted = true;
  }
  if (entry->right) {
    conflict.has_right = true;
    std::string value;
    if (!fsl::StringFromVmo(*entry->right->value, &value)) {
      FXL_LOG(ERROR) << "Unable to read vmo for right entry of "
                     << to_hex_string(conflict.key) << ".";
      return conflict;
    }
    conflict.right = std::move(value);
  } else {
    conflict.right_is_deleted = true;
  }
  return conflict;
}

void GetDiffRecursive(fuchsia::ledger::MergeResultProvider* const result,
                      std::map<std::string, PageClient::Conflict>* conflicts,
                      LedgerToken token,
                      std::function<void(fuchsia::ledger::Status)> callback) {
  auto cont = fxl::MakeCopyable(
      [result, conflicts, callback = std::move(callback)](
          fuchsia::ledger::Status status,
          fidl::VectorPtr<fuchsia::ledger::DiffEntry> change_delta,
          LedgerToken token) mutable {
        if (status != fuchsia::ledger::Status::OK &&
            status != fuchsia::ledger::Status::PARTIAL_RESULT) {
          callback(status);
          return;
        }

        for (auto& diff_entry : *change_delta) {
          (*conflicts)[to_string(diff_entry.key)] = ToConflict(&diff_entry);
        }

        if (status == fuchsia::ledger::Status::OK) {
          callback(fuchsia::ledger::Status::OK);
          return;
        }

        GetDiffRecursive(result, conflicts, std::move(token),
                         std::move(callback));
      });

  result->GetConflictingDiff(std::move(token), cont);
}

void GetDiff(fuchsia::ledger::MergeResultProvider* const result,
             std::map<std::string, PageClient::Conflict>* conflicts,
             std::function<void(fuchsia::ledger::Status)> callback) {
  GetDiffRecursive(result, conflicts, nullptr /* token */, std::move(callback));
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

class LedgerClient::ConflictResolverImpl::ResolveCall : public Operation<> {
 public:
  ResolveCall(ConflictResolverImpl* const impl,
              fuchsia::ledger::MergeResultProviderPtr result_provider,
              fuchsia::ledger::PageSnapshotPtr left_version,
              fuchsia::ledger::PageSnapshotPtr right_version,
              fuchsia::ledger::PageSnapshotPtr common_version)
      : Operation("LedgerClient::ResolveCall", [] {}),
        impl_(impl),
        result_provider_(std::move(result_provider)),
        left_version_(std::move(left_version)),
        right_version_(std::move(right_version)),
        common_version_(std::move(common_version)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    GetDiff(result_provider_.get(), &conflicts_,
            [this, flow](fuchsia::ledger::Status status) {
              has_diff_ = true;
              Cont(flow);
            });

    result_provider_->MergeNonConflictingEntries(
        [this, flow](fuchsia::ledger::Status status) {
          merged_non_conflict_ = true;
          Cont(flow);
        });

    GetEntries(left_version_.get(), &left_entries_,
               [this, flow](fuchsia::ledger::Status) {
                 LogEntries("left", left_entries_);
               });

    GetEntries(right_version_.get(), &right_entries_,
               [this, flow](fuchsia::ledger::Status) {
                 LogEntries("right", right_entries_);
               });

    GetEntries(common_version_.get(), &common_entries_,
               [this, flow](fuchsia::ledger::Status) {
                 LogEntries("common", common_entries_);
               });
  }

  void Cont(FlowToken flow) {
    if (!has_diff_ || !merged_non_conflict_) {
      return;
    }

    std::vector<PageClient*> page_clients;
    impl_->GetPageClients(&page_clients);

    for (auto& pair : conflicts_) {
      const PageClient::Conflict& conflict = pair.second;
      fidl::VectorPtr<fuchsia::ledger::MergedValue> merge_changes;

      if (conflict.has_left && conflict.has_right) {
        for (PageClient* const page_client : page_clients) {
          if (HasPrefix(pair.first, page_client->prefix())) {
            page_client->OnPageConflict(&pair.second);

            if (pair.second.resolution != PageClient::LEFT) {
              fuchsia::ledger::MergedValue merged_value;
              merged_value.key = conflict.key.Clone();
              if (pair.second.resolution == PageClient::RIGHT) {
                merged_value.source = fuchsia::ledger::ValueSource::RIGHT;
              } else {
                if (pair.second.merged_is_deleted) {
                  merged_value.source = fuchsia::ledger::ValueSource::DELETE;
                } else {
                  merged_value.source = fuchsia::ledger::ValueSource::NEW;
                  merged_value.new_value =
                      fuchsia::ledger::BytesOrReference::New();
                  merged_value.new_value->set_bytes(
                      to_array(pair.second.merged));
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
      }

      if (!merge_changes->empty()) {
        merge_count_++;
        result_provider_->Merge(std::move(merge_changes),
                                [this, flow](fuchsia::ledger::Status status) {
                                  if (status != fuchsia::ledger::Status::OK) {
                                    FXL_LOG(ERROR)
                                        << "ResultProvider.Merge() " << status;
                                  }
                                  MergeDone(flow);
                                });
      }
    }

    MergeDone(flow);
  }

  void MergeDone(FlowToken flow) {
    if (--merge_count_ == 0) {
      result_provider_->Done([this, flow](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << "ResultProvider.Done() " << status;
        }
        impl_->NotifyWatchers();
      });
    }
  }

  void LogEntries(const std::string& headline,
                  const std::vector<fuchsia::ledger::Entry>& entries) {
    FXL_VLOG(4) << "Entries " << headline;
    for (const fuchsia::ledger::Entry& entry : entries) {
      FXL_VLOG(4) << " - " << to_string(entry.key);
    }
  }

  ConflictResolverImpl* const impl_;
  fuchsia::ledger::MergeResultProviderPtr result_provider_;

  fuchsia::ledger::PageSnapshotPtr left_version_;
  fuchsia::ledger::PageSnapshotPtr right_version_;
  fuchsia::ledger::PageSnapshotPtr common_version_;

  std::vector<fuchsia::ledger::Entry> left_entries_;
  std::vector<fuchsia::ledger::Entry> right_entries_;
  std::vector<fuchsia::ledger::Entry> common_entries_;

  bool has_diff_{};
  bool merged_non_conflict_{};

  std::map<std::string, PageClient::Conflict> conflicts_;

  int merge_count_{1};  // One extra for the call at the end.

  FXL_DISALLOW_COPY_AND_ASSIGN(ResolveCall);
};

LedgerClient::LedgerClient(fuchsia::ledger::LedgerPtr ledger)
    : ledger_(std::move(ledger)) {
  ledger_->SetConflictResolverFactory(
      bindings_.AddBinding(this), [](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << "Ledger.SetConflictResolverFactory() failed: "
                         << LedgerStatusToString(status);
        }
      });
}

LedgerClient::LedgerClient(
    fuchsia::ledger::internal::LedgerRepository* const ledger_repository,
    const std::string& name, std::function<void()> error)
    : ledger_name_(name) {
  ledger_repository->Duplicate(
      ledger_repository_.NewRequest(), [error](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository::Duplicate() failed: "
                         << LedgerStatusToString(status);
          error();
        }
      });

  // Open Ledger.
  ledger_repository->GetLedger(
      to_array(name), ledger_.NewRequest(),
      [error](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository.GetLedger() failed: "
                         << LedgerStatusToString(status);
          error();
        }
      });

  // This must be the first call after GetLedger, otherwise the Ledger
  // starts with one reconciliation strategy, then switches to another.
  ledger_->SetConflictResolverFactory(
      bindings_.AddBinding(this), [error](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << "Ledger.SetConflictResolverFactory() failed: "
                         << LedgerStatusToString(status);
          error();
        }
      });
}

LedgerClient::LedgerClient(
    fuchsia::ledger::internal::LedgerRepository* const ledger_repository,
    const std::string& name)
    : ledger_name_(name) {
  ledger_repository->Duplicate(
      ledger_repository_.NewRequest(), [](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
          FXL_LOG(ERROR) << "LedgerRepository.Duplicate() failed: "
                         << LedgerStatusToString(status);

          // No further error reporting, as this is used only in tests.
        }
      });

  // Open Ledger.
  ledger_repository->GetLedger(
      to_array(name), ledger_.NewRequest(), [](fuchsia::ledger::Status status) {
        if (status != fuchsia::ledger::Status::OK) {
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

fuchsia::ledger::Page* LedgerClient::GetPage(
    PageClient* const page_client, const std::string& context,
    const fuchsia::ledger::PageId& page_id) {
  auto i = std::find_if(pages_.begin(), pages_.end(), [&page_id](auto& entry) {
    return PageIdsEqual(entry->page_id, page_id);
  });

  if (i != pages_.end()) {
    (*i)->clients.push_back(page_client);
    return (*i)->page.get();
  }

  fuchsia::ledger::PagePtr page;
  fuchsia::ledger::PageIdPtr page_id_copy = fuchsia::ledger::PageId::New();
  ;
  page_id_copy->id = page_id.id;
  ledger_->GetPage(std::move(page_id_copy), page.NewRequest(),
                   [context](fuchsia::ledger::Status status) {
                     if (status != fuchsia::ledger::Status::OK) {
                       FXL_LOG(ERROR)
                           << "Ledger.GetPage() " << context << " " << status;
                     }
                   });

  auto entry = std::make_unique<PageEntry>();
  entry->page_id = CloneStruct(page_id);
  entry->clients.push_back(page_client);

  entry->page = std::move(page);
  entry->page.set_error_handler([context] {
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

void LedgerClient::GetPolicy(LedgerPageId page_id, GetPolicyCallback callback) {
  auto i = std::find_if(pages_.begin(), pages_.end(), [&page_id](auto& entry) {
    return PageIdsEqual(entry->page_id, page_id);
  });

  // This is wrong if GetPolicy() is called for a page before its page client
  // has registered. Therefore, if an app keeps multiple connections to a page,
  // the ones kept by page clients must be created first.
  //
  // TODO(mesch): Maybe AUTOMATIC_WITH_FALLBACK should always be used anyway,
  // and the resolver should deal with conflicts on pages that don't have a page
  // client.
  if (i != pages_.end()) {
    callback(fuchsia::ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK);
    return;
  }

  callback(fuchsia::ledger::MergePolicy::LAST_ONE_WINS);
}

void LedgerClient::NewConflictResolver(
    LedgerPageId page_id,
    fidl::InterfaceRequest<fuchsia::ledger::ConflictResolver> request) {
  for (auto& i : resolvers_) {
    if (PageIdsEqual(i->page_id(), page_id)) {
      i->Connect(std::move(request));
      return;
    }
  }

  resolvers_.emplace_back(
      std::make_unique<ConflictResolverImpl>(this, std::move(page_id)));
  resolvers_.back()->Connect(std::move(request));
}

void LedgerClient::ClearConflictResolver(const LedgerPageId& page_id) {
  auto i = std::remove_if(resolvers_.begin(), resolvers_.end(),
                          [&page_id](auto& item) {
                            return PageIdsEqual(item->page_id(), page_id);
                          });

  if (i != resolvers_.end()) {
    resolvers_.erase(i, resolvers_.end());
  }
}

LedgerClient::ConflictResolverImpl::ConflictResolverImpl(
    LedgerClient* const ledger_client, const LedgerPageId& page_id)
    : ledger_client_(ledger_client), page_id_(CloneStruct(page_id)) {}

LedgerClient::ConflictResolverImpl::~ConflictResolverImpl() = default;

void LedgerClient::ConflictResolverImpl::Connect(
    fidl::InterfaceRequest<fuchsia::ledger::ConflictResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

void LedgerClient::ConflictResolverImpl::Resolve(
    fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> left_version,
    fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> right_version,
    fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> common_version,
    fidl::InterfaceHandle<fuchsia::ledger::MergeResultProvider>
        result_provider) {
  operation_queue_.Add(
      new ResolveCall(this, result_provider.Bind(), left_version.Bind(),
                      right_version.Bind(), common_version.Bind()));
}

void LedgerClient::ConflictResolverImpl::GetPageClients(
    std::vector<PageClient*>* page_clients) {
  auto i = std::find_if(
      ledger_client_->pages_.begin(), ledger_client_->pages_.end(),
      [this](auto& item) { return PageIdsEqual(item->page_id, page_id_); });

  FXL_CHECK(i != ledger_client_->pages_.end());
  *page_clients = (*i)->clients;
}

void LedgerClient::ConflictResolverImpl::NotifyWatchers() const {
  for (auto& watcher : ledger_client_->watchers_) {
    watcher();
  }
}

}  // namespace modular
