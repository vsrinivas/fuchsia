// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/tool/inspect_command.h"

#include <cctype>
#include <iostream>
#include <string>

#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/tool/convert.h"
#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tool {
namespace {
// When displaying value data, maximum number of bytes that will be displayed
// before being truncated.
const size_t kDataSizeLimit = 400;

// Returns a printable, truncated to kDataSizeLimit string from the argument.
std::string ToPrintable(ftl::StringView string) {
  for (char i : string) {
    // Check if the character is "normal" (printable, or new line) for the
    // current locale.
    if (!(isprint(i) || isspace(i))) {
      // Hex encoding takes 2 characters for each byte.
      if (string.size() > kDataSizeLimit / 2) {
        return ToHexString(string.substr(0, kDataSizeLimit / 2)) + "...";
      } else {
        return ToHexString(string);
      }
    }
  }
  if (string.size() > kDataSizeLimit) {
    return string.substr(0, kDataSizeLimit).ToString() + "...";
  } else {
    return string.ToString();
  }
}
}  // namespace

InspectCommand::InspectCommand(const std::vector<std::string>& args,
                               const cloud_sync::UserConfig& user_config,
                               ftl::StringView user_repository_path)
    : args_(args),
      app_id_(args_[1]),
      user_repository_path_(user_repository_path.ToString()) {
  FTL_DCHECK(!user_repository_path_.empty());
}

void InspectCommand::Start(ftl::Closure on_done) {
  if (args_.size() == 3 && args_[2] == "pages") {
    ListPages(std::move(on_done));
  } else if (args_.size() == 5 && args_[2] == "commit") {
    DisplayCommit(std::move(on_done));
  } else {
    PrintHelp(std::move(on_done));
  }
}

void InspectCommand::ListPages(ftl::Closure on_done) {
  std::cout << "List of pages for app " << app_id_ << ":" << std::endl;
  std::unique_ptr<storage::LedgerStorageImpl> ledger_storage(
      GetLedgerStorage());
  std::vector<storage::PageId> page_ids = ledger_storage->ListLocalPages();
  auto waiter = callback::CompletionWaiter::Create();
  for (const storage::PageId& page_id : page_ids) {
    ledger_storage->GetPageStorage(
        page_id, ftl::MakeCopyable([
          completer = ftl::MakeAutoCall(waiter->NewCallback()), page_id
        ](storage::Status status,
                 std::unique_ptr<storage::PageStorage> storage) {
          if (status != storage::Status::OK) {
            FTL_LOG(FATAL) << "Unable to retrieve page " << ToHexString(page_id)
                           << " due to error " << status;
          }
          std::cout << "Page " << ToHexString(page_id) << std::endl;
          std::vector<storage::CommitId> heads;
          storage::Status get_status = storage->GetHeadCommitIds(&heads);
          if (get_status != storage::Status::OK) {
            FTL_LOG(FATAL) << "Unable to retrieve commits for page "
                           << ToHexString(page_id) << " due to error "
                           << status;
          }
          for (const storage::CommitId& commit_id : heads) {
            std::cout << " head commit " << ToHexString(commit_id) << std::endl;
          }
        }));
  }
  waiter->Finalize(std::move(on_done));
}

void InspectCommand::DisplayCommit(ftl::Closure on_done) {
  std::unique_ptr<storage::LedgerStorageImpl> ledger_storage(
      GetLedgerStorage());
  storage::PageId page_id;
  if (!FromHexString(args_[3], &page_id)) {
    FTL_LOG(ERROR) << "Unable to parse page id " << args_[3];
    on_done();
    return;
  }
  storage::CommitId commit_id;
  if (!FromHexString(args_[4], &commit_id)) {
    FTL_LOG(ERROR) << "Unable to parse commit id " << args_[4];
    on_done();
    return;
  }
  FTL_LOG(INFO) << "Commit id " << ToHexString(commit_id);
  ledger_storage->GetPageStorage(page_id, [
    this, commit_id, on_done = std::move(on_done)
  ](storage::Status status, std::unique_ptr<storage::PageStorage> storage) {
    if (status != storage::Status::OK) {
      FTL_LOG(ERROR) << "Unable to retrieve page due to error " << status;
      on_done();
      return;
    }
    storage_ = std::move(storage);
    storage_->GetCommit(commit_id, [
      this, commit_id, on_done = std::move(on_done)
    ](storage::Status status, std::unique_ptr<const storage::Commit> commit) {
      if (status != storage::Status::OK) {
        FTL_LOG(ERROR) << "Unable to retrieve commit " << ToHexString(commit_id)
                       << " on page " << ToHexString(storage_->GetId())
                       << " due to error " << status;
        on_done();
        return;
      }
      PrintCommit(std::move(commit), std::move(on_done));
    });
  });
}

void InspectCommand::PrintCommit(std::unique_ptr<const storage::Commit> commit,
                                 ftl::Closure on_done) {
  // Print commit info
  std::cout << "Commit " << args_[4] << std::endl;
  std::cout << " timestamp " << commit->GetTimestamp() << std::endl;
  for (storage::CommitIdView parent_commit : commit->GetParentIds()) {
    std::cout << " parent " << ToHexString(parent_commit) << std::endl;
  }
  std::cout << "Page state at this commit: " << std::endl;
  coroutine_service_.StartCoroutine(ftl::MakeCopyable([
    this, commit = std::move(commit), on_done = std::move(on_done)
  ](coroutine::CoroutineHandler * handler) mutable {
    storage_->GetCommitContents(
        *commit, "",
        [this, handler](storage::Entry entry) {
          storage::Status status;
          std::unique_ptr<const storage::Object> object;
          if (coroutine::SyncCall(
                  handler,
                  [this, &entry](
                      const std::function<void(
                          storage::Status,
                          std::unique_ptr<const storage::Object>)>& callback) {
                    storage_->GetObject(entry.object_id,
                                        storage::PageStorage::Location::LOCAL,
                                        std::move(callback));
                  },
                  &status, &object)) {
            FTL_NOTREACHED();
          }
          std::string priority_str =
              entry.priority == storage::KeyPriority::EAGER ? "EAGER" : "LAZY";
          ftl::StringView data;
          object->GetData(&data);
          std::cout << " Key " << entry.key << " (" << priority_str << "): ";
          std::cout << ToPrintable(data) << std::endl;
          return true;
        },
        [on_done = std::move(on_done)](storage::Status status) {
          if (status != storage::Status::OK) {
            FTL_LOG(FATAL) << "Unable to retrieve commit contents due to error "
                           << status;
          }
          on_done();
        });
  }));
}

void InspectCommand::PrintHelp(ftl::Closure on_done) {
  std::cout
      << "inspect command: inspects the contents of a ledger.\n"
      << "Note: you must stop Ledger before running this tool.\n\n"
      << "Syntax: ledger_tool inspect <app_id> (pages|commit <page_id> "
         "<commit_id>)\n\n"
      << "Parameters:\n"
      << " - app_id: ID of the application to inspect\n"
      << "           e.g.: modular_user_runner\n"
      << " - pages: list all pages available locally, with their head commits\n"
      << " - commit <page_id> <commit_id>: list the full contents at the "
         "commit from the given page."
      << std::endl;
  on_done();
}

std::unique_ptr<storage::LedgerStorageImpl> InspectCommand::GetLedgerStorage() {
  return std::make_unique<storage::LedgerStorageImpl>(
      mtl::MessageLoop::GetCurrent()->task_runner(),
      mtl::MessageLoop::GetCurrent()->task_runner(), &coroutine_service_,
      user_repository_path_, app_id_);
}

}  // namespace tool
