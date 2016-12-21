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
    int64_t change_timestamp,
    std::unique_ptr<storage::CommitContents> base,
    std::unique_ptr<storage::CommitContents> other,
    std::function<void(storage::Status, PageChangePtr)> callback) {
  base->diff(
      std::move(other),
      [ change_timestamp, storage, callback = std::move(callback) ](
          storage::Status status,
          std::unique_ptr<storage::Iterator<const storage::EntryChange>>
              it) mutable {
        if (status != storage::Status::OK) {
          FTL_LOG(ERROR) << "Unable to compute diff.";
          callback(status, nullptr);
          return;
        }

        if (!it->Valid()) {
          callback(storage::Status::OK, nullptr);
          return;
        }

        auto waiter = callback::Waiter<storage::Status,
                                       std::unique_ptr<const storage::Object>>::
            Create(storage::Status::OK);

        PageChangePtr page_change = PageChange::New();
        page_change->timestamp = change_timestamp;
        page_change->changes = fidl::Array<EntryPtr>::New(0);
        page_change->deleted_keys = fidl::Array<fidl::Array<uint8_t>>::New(0);

        for (; it->Valid(); it->Next()) {
          if ((*it)->deleted) {
            page_change->deleted_keys.push_back(
                convert::ToArray((*it)->entry.key));
            continue;
          }

          EntryPtr entry = Entry::New();
          entry->key = convert::ToArray((*it)->entry.key);
          entry->priority = (*it)->entry.priority == storage::KeyPriority::EAGER
                                ? Priority::EAGER
                                : Priority::LAZY;
          page_change->changes.push_back(std::move(entry));
          storage->GetObject((*it)->entry.object_id, waiter->NewCallback());
        }

        std::function<void(storage::Status,
                           std::vector<std::unique_ptr<const storage::Object>>)>
            result_callback = ftl::MakeCopyable([
              page_change = std::move(page_change),
              callback = std::move(callback)
            ](storage::Status status,
              std::vector<std::unique_ptr<const storage::Object>>
                  results) mutable {
              if (status != storage::Status::OK) {
                FTL_LOG(ERROR)
                    << "Watcher: error while reading changed values.";
                callback(status, nullptr);
                return;
              }
              FTL_DCHECK(results.size() == page_change->changes.size());
              for (size_t i = 0; i < results.size(); i++) {
                ftl::StringView object_contents;

                storage::Status read_status =
                    results[i]->GetData(&object_contents);
                if (read_status != storage::Status::OK) {
                  FTL_LOG(ERROR)
                      << "Watcher: error while reading changed value.";
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
}

}  // namespace diff_utils
}  // namespace ledger
