// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/history.h"

#include <zircon/syscalls.h>

#include <sys/time.h>

#include <algorithm>
#include <utility>

#include "garnet/bin/moterm/ledger_helpers.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"

namespace moterm {

namespace {
constexpr int kMaxHistorySize = 1000;

std::string ToString(fidl::Array<uint8_t>& data) {
  std::string ret;
  ret.resize(data.size());
  memcpy(&ret[0], data.data(), data.size());
  return ret;
}

std::string ToString(const zx::vmo& value) {
  std::string ret;
  if (!fsl::StringFromVmo(value, &ret)) {
    FXL_DCHECK(false);
  }
  return ret;
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

using Key = fidl::Array<uint8_t>;

std::string MakeKey() {
  return fxl::StringPrintf("%120lu-%u", zx_time_get(ZX_CLOCK_UTC), rand());
}

void GetMoreEntries(
    ledger::PageSnapshot* snapshot,
    fidl::Array<uint8_t> token,
    std::vector<ledger::EntryPtr> existing_entries,
    std::function<void(ledger::Status, std::vector<ledger::EntryPtr>)>
        callback) {
  snapshot->GetEntries(
      nullptr, std::move(token), fxl::MakeCopyable([
        snapshot, existing_entries = std::move(existing_entries),
        callback = std::move(callback)
      ](ledger::Status status, auto entries, auto next_token) mutable {
        if (status != ledger::Status::OK &&
            status != ledger::Status::PARTIAL_RESULT) {
          FXL_LOG(ERROR) << "GetEntries failed";
          callback(status, {});
          return;
        }
        for (auto& entry : entries) {
          existing_entries.push_back(std::move(entry));
        }
        if (!next_token) {
          FXL_DCHECK(status == ledger::Status::OK);
          callback(ledger::Status::OK, std::move(existing_entries));
          return;
        }

        FXL_DCHECK(status == ledger::Status::PARTIAL_RESULT);
        GetMoreEntries(snapshot, std::move(next_token),
                       std::move(existing_entries), std::move(callback));
      }));
}

// Retrieves all entries from the given snapshot, concatenating the paginated
// response if needed.
void GetEntries(ledger::PageSnapshot* snapshot,
                std::function<void(ledger::Status,
                                   std::vector<ledger::EntryPtr>)> callback) {
  GetMoreEntries(snapshot, nullptr, {}, std::move(callback));
}

}  // namespace

History::History() : page_watcher_binding_(this) {}
History::~History() {}

void History::Initialize(ledger::PagePtr page) {
  FXL_DCHECK(!initialized_);
  initialized_ = true;
  page_ = std::move(page);

  for (auto& pending_callback : pending_read_entries_) {
    DoReadEntries(std::move(pending_callback));
  }
  pending_read_entries_.clear();
}

void History::ReadInitialEntries(
    std::function<void(std::vector<std::string>)> callback) {
  if (!initialized_) {
    pending_read_entries_.push_back(std::move(callback));
    return;
  }

  DoReadEntries(std::move(callback));
}

void History::DoReadEntries(
    std::function<void(std::vector<std::string>)> callback) {
  FXL_DCHECK(!snapshot_.is_bound());
  if (!page_) {
    FXL_LOG(WARNING)
        << "Ignoring a call to retrieve history. (running outside of story?)";
    callback({});
    return;
  }

  page_->GetSnapshot(snapshot_.NewRequest(), nullptr,
                     page_watcher_binding_.NewBinding(),
                     LogLedgerErrorCallback("GetSnapshot"));
  GetEntries(snapshot_.get(), [callback = std::move(callback)](
                                  ledger::Status status,
                                  std::vector<ledger::EntryPtr> entries) {
    if (status != ledger::Status::OK) {
      FXL_LOG(ERROR) << "Failed to retrieve the history entries from Ledger.";
      callback({});
      return;
    }

    std::vector<std::string> results;
    for (auto& entry : entries) {
      results.push_back(ToString(entry->value));
    }
    callback(std::move(results));
  });
}

void History::AddEntry(const std::string& entry) {
  if (!page_) {
    return;
  }

  std::string key = MakeKey();
  page_->Put(ToArray(key), ToArray(entry), LogLedgerErrorCallback("Put"));
  local_entry_keys_.insert(std::move(key));
  Trim();
}

void History::RegisterClient(Client* client) {
  clients_.insert(client);
}

void History::UnregisterClient(Client* client) {
  clients_.erase(client);
}

void History::OnChange(ledger::PageChangePtr page_change,
                       ledger::ResultState result_state,
                       const OnChangeCallback& callback) {
  FXL_DCHECK(result_state == ledger::ResultState::COMPLETED);
  for (auto& entry : page_change->changes) {
    if (local_entry_keys_.count(ToString(entry->key)) == 0) {
      // Notify clients about the remote entry.
      for (Client* client : clients_) {
        client->OnRemoteEntry(ToString(entry->value));
      }
    } else {
      local_entry_keys_.erase(ToString(entry->key));
    }
  }
  callback({});
}

void History::Trim() {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), nullptr, nullptr,
                     LogLedgerErrorCallback("GetSnapshot"));
  ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  GetEntries(snapshot_ptr,
             fxl::MakeCopyable([ this, snapshot = std::move(snapshot) ](
                 ledger::Status status, std::vector<ledger::EntryPtr> entries) {
               if (status != ledger::Status::OK) {
                 FXL_LOG(ERROR)
                     << "Failed to retrieve the history entries from Ledger.";
                 return;
               }

               if (entries.size() > kMaxHistorySize) {
                 size_t remove_count = entries.size() - kMaxHistorySize;
                 for (size_t i = 0; i < remove_count; i++) {
                   page_->Delete(std::move(entries[i]->key),
                                 LogLedgerErrorCallback("Delete"));
                 }
               }
             }));
}

}  // namespace moterm
