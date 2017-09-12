// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/diff_utils.h"

#include <limits>
#include <memory>
#include <vector>

#include "apps/ledger/src/app/fidl/serialization_size.h"
#include "apps/ledger/src/app/page_utils.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/storage/public/object.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/vmo/strings.h"

namespace ledger {
namespace diff_utils {

void ComputePageChange(
    storage::PageStorage* storage,
    const storage::Commit& base,
    const storage::Commit& other,
    std::string prefix_key,
    std::string min_key,
    PaginationBehavior pagination_behavior,
    std::function<void(Status, std::pair<PageChangePtr, std::string>)>
        callback) {
  struct Context {
    // The PageChangePtr to be returned through the callback.
    PageChangePtr page_change = PageChange::New();
    // The serialization size of all entries.
    size_t fidl_size = fidl_serialization::kPageChangeHeaderSize;
    // The number of handles.
    size_t handles_count = 0u;
    // The next token to be returned through the callback.
    std::string next_token = "";
  };

  auto waiter = callback::Waiter<Status, mx::vmo>::Create(Status::OK);

  auto context = std::make_unique<Context>();
  context->page_change->timestamp = other.GetTimestamp();
  context->page_change->changes = fidl::Array<EntryPtr>::New(0);
  context->page_change->deleted_keys =
      fidl::Array<fidl::Array<uint8_t>>::New(0);

  if (min_key < prefix_key) {
    min_key = prefix_key;
  }

  // |on_next| is called for each change on the diff
  auto on_next = [
    storage, waiter, prefix_key = std::move(prefix_key),
    context = context.get(), pagination_behavior
  ](storage::EntryChange change) {
    if (!PageUtils::MatchesPrefix(change.entry.key, prefix_key)) {
      return false;
    }
    size_t entry_size =
        change.deleted
            ? fidl_serialization::GetByteArraySize(change.entry.key.size())
            : fidl_serialization::GetEntrySize(change.entry.key.size());
    size_t entry_handle_count = change.deleted ? 0 : 1;
    if (pagination_behavior == PaginationBehavior::BY_SIZE &&
        (context->fidl_size + entry_size >
             fidl_serialization::kMaxInlineDataSize ||
         context->handles_count + entry_handle_count >
             fidl_serialization::kMaxMessageHandles

         )) {
      context->next_token = change.entry.key;
      return false;
    }
    context->fidl_size += entry_size;
    context->handles_count += entry_handle_count;
    if (change.deleted) {
      context->page_change->deleted_keys.push_back(
          convert::ToArray(change.entry.key));
      return true;
    }

    EntryPtr entry = Entry::New();
    entry->key = convert::ToArray(change.entry.key);
    entry->priority = change.entry.priority == storage::KeyPriority::EAGER
                          ? Priority::EAGER
                          : Priority::LAZY;
    context->page_change->changes.push_back(std::move(entry));
    PageUtils::GetPartialReferenceAsBuffer(
        storage, change.entry.object_id, 0u,
        std::numeric_limits<int64_t>::max(),
        storage::PageStorage::Location::LOCAL, Status::OK,
        waiter->NewCallback());
    return true;
  };

  // |on_done| is called when the full diff is computed.
  auto on_done = fxl::MakeCopyable([
    waiter = std::move(waiter), context = std::move(context),
    callback = std::move(callback)
  ](storage::Status status) mutable {
    if (status != storage::Status::OK) {
      FXL_LOG(ERROR) << "Unable to compute diff for PageChange: " << status;
      callback(PageUtils::ConvertStatus(status), std::make_pair(nullptr, ""));
      return;
    }
    if (context->page_change->changes.empty()) {
      if (context->page_change->deleted_keys.empty()) {
        callback(Status::OK, std::make_pair(nullptr, ""));
      } else {
        callback(Status::OK,
                 std::make_pair(std::move(context->page_change), ""));
      }
      return;
    }

    // We need to retrieve the values for each changed key/value pair in order
    // to send it inside the PageChange object. |waiter| collates these
    // asynchronous calls and |result_callback| processes them.
    auto result_callback = fxl::MakeCopyable([
      context = std::move(context), callback = std::move(callback)
    ](Status status, std::vector<mx::vmo> results) mutable {
      if (status != Status::OK) {
        FXL_LOG(ERROR)
            << "Error while reading changed values when computing PageChange: "
            << status;
        callback(status, std::make_pair(nullptr, ""));
        return;
      }
      FXL_DCHECK(results.size() == context->page_change->changes.size());
      for (size_t i = 0; i < results.size(); i++) {
        context->page_change->changes[i]->value = std::move(results[i]);
      }
      callback(Status::OK, std::make_pair(std::move(context->page_change),
                                          std::move(context->next_token)));
    });
    waiter->Finalize(std::move(result_callback));
  });
  storage->GetCommitContentsDiff(base, other, std::move(min_key),
                                 std::move(on_next), std::move(on_done));
}

}  // namespace diff_utils
}  // namespace ledger
