// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/diff_utils.h"

#include <memory>
#include <vector>

#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/storage/public/object.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {
namespace diff_utils {

void ComputePageChange(
    storage::PageStorage* storage,
    const storage::Commit& base,
    const storage::Commit& other,
    std::function<void(storage::Status, PageChangePtr)> callback) {
  auto waiter = callback::
      Waiter<storage::Status, std::unique_ptr<const storage::Object>>::Create(
          storage::Status::OK);

  PageChangePtr page_change = PageChange::New();
  page_change->timestamp = other.GetTimestamp();
  page_change->changes = fidl::Array<EntryPtr>::New(0);
  page_change->deleted_keys = fidl::Array<fidl::Array<uint8_t>>::New(0);

  // |on_next| is called for each change on the diff
  auto on_next = [ storage, waiter, page_change = page_change.get() ](
      storage::EntryChange change) {
    if (change.deleted) {
      page_change->deleted_keys.push_back(convert::ToArray(change.entry.key));
      return true;
    }

    EntryPtr entry = Entry::New();
    entry->key = convert::ToArray(change.entry.key);
    entry->priority = change.entry.priority == storage::KeyPriority::EAGER
                          ? Priority::EAGER
                          : Priority::LAZY;
    page_change->changes.push_back(std::move(entry));
    // TODO(etiennej): What do we do if we don't have the network (e.g. for
    // lazy objects)?
    storage->GetObject(change.entry.object_id,
                       storage::PageStorage::Location::NETWORK,
                       waiter->NewCallback());

    return true;
  };

  // |on_done| is called when the full diff is computed.
  auto on_done = ftl::MakeCopyable([
    waiter = std::move(waiter), page_change = std::move(page_change),
    callback = std::move(callback)
  ](storage::Status status) mutable {
    if (status != storage::Status::OK) {
      FTL_LOG(ERROR) << "Unable to compute diff for PageChange: " << status;
      callback(status, nullptr);
      return;
    }
    if (page_change->changes.size() == 0 &&
        page_change->deleted_keys.size() == 0) {
      callback(status, nullptr);
      return;
    }

    // We need to retrieve the values for each changed key/value pair in order
    // to send it inside the PageChange object. |waiter| collates these
    // asynchronous calls and |result_callback| processes them.
    auto result_callback = ftl::MakeCopyable([
      page_change = std::move(page_change), callback = std::move(callback)
    ](storage::Status status,
      std::vector<std::unique_ptr<const storage::Object>> results) mutable {
      if (status != storage::Status::OK) {
        FTL_LOG(ERROR)
            << "Error while reading changed values when computing PageChange: "
            << status;
        callback(status, nullptr);
        return;
      }
      FTL_DCHECK(results.size() == page_change->changes.size());
      for (size_t i = 0; i < results.size(); i++) {
        ftl::StringView object_contents;

        storage::Status read_status = results[i]->GetData(&object_contents);
        if (read_status != storage::Status::OK) {
          FTL_LOG(ERROR) << "Error while reading changed value for key "
                         << convert::ExtendedStringView(
                                page_change->changes[i]->key)
                         << " when computing PageChange: " << read_status;
          callback(read_status, nullptr);
          return;
        }

        // TODO(etiennej): LE-75 implement pagination on OnChange.
        // TODO(etiennej): LE-120 Use VMOs for big values.
        page_change->changes[i]->value = Value::New();
        page_change->changes[i]->value->set_bytes(
            convert::ToArray(object_contents));
      }
      callback(storage::Status::OK, std::move(page_change));
    });
    waiter->Finalize(result_callback);
  });
  storage->GetCommitContentsDiff(base, other, std::move(on_next),
                                 std::move(on_done));
}

}  // namespace diff_utils
}  // namespace ledger
