// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/history.h"

#include <magenta/syscalls.h>

#include <sys/time.h>

#include <algorithm>
#include <utility>

#include "apps/moterm/ledger_helpers.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace moterm {

namespace {
constexpr int kMaxHistorySize = 1000;

std::string ToString(const mx::vmo& value) {
  std::string ret;
  if (!mtl::StringFromVmo(value, &ret)) {
    FTL_DCHECK(false);
  }
  return ret;
}

fidl::Array<uint8_t> ToArray(const std::string& val) {
  auto ret = fidl::Array<uint8_t>::New(val.size());
  memcpy(ret.data(), val.data(), val.size());
  return ret;
}

using Key = fidl::Array<uint8_t>;

Key MakeKey() {
  return ToArray(
      ftl::StringPrintf("%120lu-%u", mx_time_get(MX_CLOCK_UTC), rand()));
}

void GetMoreEntries(
    ledger::PageSnapshotPtr snapshot,
    fidl::Array<uint8_t> token,
    std::vector<ledger::EntryPtr> existing_entries,
    std::function<void(ledger::Status, std::vector<ledger::EntryPtr>)>
        callback) {
  ledger::PageSnapshot* snapshot_ptr = snapshot.get();
  snapshot_ptr->GetEntries(
      nullptr, std::move(token), ftl::MakeCopyable([
        snapshot = std::move(snapshot),
        existing_entries = std::move(existing_entries),
        callback = std::move(callback)
      ](ledger::Status status, auto entries, auto next_token) mutable {
        if (status != ledger::Status::OK &&
            status != ledger::Status::PARTIAL_RESULT) {
          FTL_LOG(ERROR) << "GetEntries failed";
          callback(status, {});
          return;
        }
        for (auto& entry : entries) {
          existing_entries.push_back(std::move(entry));
        }
        if (!next_token) {
          FTL_DCHECK(status == ledger::Status::OK);
          callback(ledger::Status::OK, std::move(existing_entries));
          return;
        }

        FTL_DCHECK(status == ledger::Status::PARTIAL_RESULT);
        GetMoreEntries(std::move(snapshot), std::move(next_token),
                       std::move(existing_entries), std::move(callback));
      }));
}

// Retrieves all entries from the given snapshot, concatenating the paginated
// response if needed.
void GetEntries(ledger::PageSnapshotPtr snapshot,
                std::function<void(ledger::Status,
                                   std::vector<ledger::EntryPtr>)> callback) {
  GetMoreEntries(std::move(snapshot), nullptr, {}, std::move(callback));
}

}  // namespace

History::History() {}
History::~History() {}

void History::Initialize(ledger::PagePtr page) {
  FTL_DCHECK(!initialized_);
  initialized_ = true;
  page_ = std::move(page);

  for (auto& pending_callback : pending_read_entries_) {
    DoReadEntries(std::move(pending_callback));
  }
  pending_read_entries_.clear();
}

void History::ReadEntries(
    std::function<void(std::vector<std::string>)> callback) {
  if (!initialized_) {
    pending_read_entries_.push_back(std::move(callback));
    return;
  }

  DoReadEntries(std::move(callback));
}

void History::DoReadEntries(
    std::function<void(std::vector<std::string>)> callback) {
  if (!page_) {
    FTL_LOG(WARNING)
        << "Ignoring a call to retrieve history. (running outside of story?)";
    callback({});
    return;
  }

  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), nullptr,
                     LogLedgerErrorCallback("GetSnapshot"));
  GetEntries(std::move(snapshot), [callback = std::move(callback)](
                                      ledger::Status status,
                                      std::vector<ledger::EntryPtr> entries) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << "Failed to retrieve the history entries from Ledger.";
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

  page_->Put(MakeKey(), ToArray(entry), LogLedgerErrorCallback("Put"));
  Trim();
}

void History::Trim() {
  ledger::PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), nullptr,
                     LogLedgerErrorCallback("GetSnapshot"));
  GetEntries(std::move(snapshot), [this](
                                      ledger::Status status,
                                      std::vector<ledger::EntryPtr> entries) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR) << "Failed to retrieve the history entries from Ledger.";
      return;
    }

    int remove_count =
        std::max(static_cast<int>(entries.size()) - kMaxHistorySize, 0);
    for (size_t i = 0; i < static_cast<size_t>(remove_count); i++) {
      page_->Delete(std::move(entries[i]->key),
                    LogLedgerErrorCallback("Delete"));
    }
  });
}

}  // namespace moterm
